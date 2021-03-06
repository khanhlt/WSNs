/*
 * gpsr.cc
 *
 *  Last edited on Mar 18, 2015
 *  by Trong Nguyen
 */

#include "gpsr.h"

#include "../include/tcl.h"

int hdr_hello::offset_;
int hdr_gpsr::offset_;

/*
 * HELLO Header Class
 */
static class HelloHeaderClass : public PacketHeaderClass {
public:
    HelloHeaderClass() : PacketHeaderClass("PacketHeader/HELLO", sizeof(hdr_hello)) {
        bind_offset(&hdr_hello::offset_);
    }

    ~HelloHeaderClass() {}
} class_hellohdr;

/*
 * GPSR Header Class
 */
static class GPSRHeaderClass : public PacketHeaderClass {
public:
    GPSRHeaderClass() : PacketHeaderClass("PacketHeader/GPSR", sizeof(hdr_gpsr)) {
        bind_offset(&hdr_gpsr::offset_);
    }

    ~GPSRHeaderClass() {}
} class_gpsrhdr;

/*
 * GPSR Agent Class
 */
static class GPSRAgentClass : public TclClass {
public:
    GPSRAgentClass() : TclClass("Agent/GPSR") {}

    TclObject *create(int, const char *const *) {
        return new GPSRAgent();
    }
} class_gpsr;

/*
 * Timer
 */
void AgentBroadcastTimer::expire(Event *e) {
    agent_->forwardBroadcast(packet_);
}

void
GPSRHelloTimer::expire(Event *e) {
    agent_->hellotout();
}

// --------------- Agent --------------- //

GPSRAgent::GPSRAgent() : Agent(PT_GPSR), hello_timer_(this) {
    data_pkt_counter_ = 0;
    hello_counter_ = 0;
    boundhole_pkt_counter_ = 0;
    HBA_counter_ = 0;
    HCI_counter_ = 0;

    my_id_ = -1;
    x_ = -1;
    y_ = -1;

    node_ = NULL;
    port_dmux_ = NULL;
    trace_target_ = NULL;
    neighbor_list_ = NULL;

    dest = new Point();
    bind("hello_period_", &hello_period_);
}

int
GPSRAgent::command(int argc, const char *const *argv) {
    if (argc == 2) {
        if (strcasecmp(argv[1], "start") == 0) {
            startUp();
            return TCL_OK;
        }
        if (strcasecmp(argv[1], "dumpEnergy") == 0) {
            dumpEnergyByTime();
            return TCL_OK;
        }
        if (strcasecmp(argv[1], "dumpBroadcast") == 0) {
            return TCL_OK;
        }
        if (strcasecmp(argv[1], "dump") == 0) {
            dumpNeighbor();
            dumpEnergy();
            dumpPktOverhead();
            return TCL_OK;
        }
        if (strcasecmp(argv[1], "nodeoff") == 0) {
            hello_timer_.force_cancel();
            if (node_->energy_model()) {
                node_->energy_model()->update_off_time(true);
            }
        }
    }

    if (argc == 3) {
        if (strcasecmp(argv[1], "addr") == 0) {
            my_id_ = Address::instance().str2addr(argv[2]);
            return TCL_OK;
        }

        TclObject *obj;
        if ((obj = TclObject::lookup(argv[2])) == 0) {
            fprintf(stderr, "%s: %s lookup of %s failed\n", __FILE__, argv[1], argv[2]);
            return (TCL_ERROR);
        }
        if (strcasecmp(argv[1], "node") == 0) {
            node_ = (MobileNode *) obj;
            return (TCL_OK);
        } else if (strcasecmp(argv[1], "port-dmux") == 0) {
            port_dmux_ = (PortClassifier *) obj; //(NsObject *) obj;
            return (TCL_OK);
        } else if (strcasecmp(argv[1], "tracetarget") == 0) {
            trace_target_ = (Trace *) obj;
            return TCL_OK;
        }
    }// if argc == 3

    return (Agent::command(argc, argv));
}

void
GPSRAgent::recv(Packet *p, Handler *h) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_ip *iph = HDR_IP(p);

    switch (cmh->ptype()) {
        case PT_HELLO:
            recvHello(p);
            break;

        case PT_CBR:
            if (iph->saddr() == my_id_)                // a packet generated by myself
            {
                if (cmh->num_forwards() == 0)        // a new packet
                {
                    sendGPSR(p);
                    recvGPSR(p);
                } else    //(cmh->num_forwards() > 0)	// routing loop -> drop
                {
                    drop(p, DROP_RTR_ROUTE_LOOP);
                }
            } else {
                iph->ttl_--;
                if (iph->ttl_ == 0) {
                    drop(p, DROP_RTR_TTL);
                    return;
                }
                recvGPSR(p);
            }
            break;

        default:
            drop(p, " UnknowType");
            break;
    }
}

void
GPSRAgent::startUp() {
    this->x_ = node_->X();        // get Location
    this->y_ = node_->Y();
    dest->x_ = node_->destX();
    dest->y_ = node_->destY();

    //hello_timer_.resched(randSend_.uniform(0.0, 40.0));
    hello_timer_.resched(5 + 0.015 * my_id_);

    FILE *fp;
    fp = fopen("Neighbors.tr", "w");
    fclose(fp);
    fp = fopen("RedundantNode.tr", "w");
    fclose(fp);
    fp = fopen("PacketOverhead.tr", "w");
    fprintf(fp, "Hello\tBoundhole\tHBA\tHCI\tData\n");
    fclose(fp);

    if (node_->energy_model()) {
        fp = fopen("Energy.tr", "w");
        fclose(fp);
        fp = fopen("EnergyByTime.tr", "w");
        fclose(fp);
        fp = fopen("DiedEnergy.tr", "w");
        fclose(fp);
    }
}

// ------------------------ Neighbor ------------------------ //

void
GPSRAgent::addNeighbor(nsaddr_t nid, Point location) {
    neighbor *temp = getNeighbor(nid);

    if (temp == NULL)            // it is a new node
    {
        temp = new neighbor;
        temp->id_ = nid;
        temp->x_ = location.x_;
        temp->y_ = location.y_;
        temp->time_ = Scheduler::instance().clock();
        temp->next_ = NULL;

        if (neighbor_list_ == NULL)        // the list now is empty
        {
            neighbor_list_ = temp;
        } else                        // the nodes list is not empty
        {
            Angle angle = G::angle(this, neighbor_list_, this, temp);
            node *i;
            for (i = neighbor_list_; i->next_; i = i->next_) {
                if (G::angle(this, neighbor_list_, this, i->next_) > angle) {
                    temp->next_ = i->next_;
                    i->next_ = temp;
                    break;
                }
            }

            if (i->next_ == NULL)    // if angle is maximum, add temp to end of neighobrs list
            {
                i->next_ = temp;
            }
        }
    } else // temp != null
    {
        temp->time_ = NOW;
        temp->x_ = location.x_;
        temp->y_ = location.y_;
    }
}

neighbor *
GPSRAgent::getNeighbor(nsaddr_t nid) {
    for (neighbor *temp = neighbor_list_; temp; temp = (neighbor *) temp->next_) {
        if (temp->id_ == nid) return temp;
    }
    return NULL;
}

neighbor *
GPSRAgent::getNeighborByGreedy(Point d, Point s) {
    //initializing the minimal distance as my distance to sink
    double mindis = G::distance(s, d);
    neighbor *re = NULL;

    for (node *temp = neighbor_list_; temp; temp = temp->next_) {
        double dis = G::distance(temp, d);
        if (dis < mindis) {
            mindis = dis;
            re = (neighbor *) temp;
        }
    }
    return re;
}

neighbor *
GPSRAgent::getNeighborByPerimeter(Point p) {
    Angle max_angle = -1;
    neighbor *nb = NULL;

    for (node *temp = neighbor_list_; temp; temp = temp->next_) {
        //if (temp->planar_)
        {
            Angle a = G::angle(this, &p, this, temp);
            if (a > max_angle) {
                max_angle = a;
                nb = (neighbor *) temp;
            }
        }
    }

    return nb;
}

// ------------------------------------- //

void
GPSRAgent::hellotout() {
    sendHello();
    if (hello_period_ > 0) hello_timer_.resched(hello_period_);

}

void
GPSRAgent::sendHello() {
    if (my_id_ < 0) return;

    Packet *p = allocpkt();
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_ip *iph = HDR_IP(p);
    struct hdr_hello *ghh = HDR_HELLO(p);

    cmh->ptype() = PT_HELLO;
    cmh->next_hop_ = IP_BROADCAST;
    cmh->last_hop_ = my_id_;
    cmh->addr_type_ = NS_AF_INET;
    cmh->size() += IP_HDR_LEN + ghh->size();

    iph->daddr() = IP_BROADCAST;
    iph->saddr() = my_id_;
    iph->sport() = RT_PORT;
    iph->dport() = RT_PORT;
    iph->ttl_ = IP_DEF_TTL;

    ghh->location_ = *this;

    send(p, 0);
    hello_counter_++;

}

void
GPSRAgent::recvHello(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_hello *ghh = HDR_HELLO(p);

    addNeighbor(cmh->last_hop_, ghh->location_);

    drop(p, "HELLO");
}

// ------------------------------------- //

void
GPSRAgent::sendGPSR(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_ip *iph = HDR_IP(p);
    struct hdr_gpsr *gdh = HDR_GPSR(p);

    cmh->size() += IP_HDR_LEN + gdh->size();
    cmh->direction() = hdr_cmn::DOWN;
    cmh->addr_type() = NS_AF_INET;

    gdh->type_ = GPSR_GPSR;
    gdh->dest_ = *dest;
    gdh->daddr_ = iph->daddr();

    iph->saddr() = my_id_;
    iph->daddr() = -1;
    iph->ttl_ = 6 * IP_DEF_TTL;    // max hop-count
}

void
GPSRAgent::recvGPSR(Packet *p) {
    struct hdr_gpsr *gdh = HDR_GPSR(p);
    recvGPSR(p, gdh);
}

void
GPSRAgent::recvGPSR(Packet *p, hdr_gpsr *gdh) {
    struct hdr_cmn *cmh = HDR_CMN(p);


    if (cmh->direction() == hdr_cmn::UP && gdh->daddr_ == my_id_) {
        port_dmux_->recv(p, 0);
    } else {
        node *nb;

        switch (gdh->type_) {
            case GPSR_GPSR:
                nb = getNeighborByGreedy(gdh->dest_, *this);

                if (nb == NULL) {
                    nb = getNeighborByPerimeter(gdh->dest_);

                    if (nb == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    } else {
                        gdh->type_ = GPSR_PERIME;
                        gdh->peri_ = *this;
                    }
                }
                break;

            case GPSR_PERIME:
                // try to get back to greedy mode
                nb = getNeighborByGreedy(gdh->dest_, gdh->peri_);
                if (nb) {
                    gdh->type_ = GPSR_GPSR;
                } else {
                    nb = getNeighborByPerimeter(gdh->prev_);
                    if (nb == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }
                }
                break;

            default:
                drop(p, " UnknowType");
                return;
        }

        gdh->prev_ = *this;

        cmh->direction_ = hdr_cmn::DOWN;
        cmh->last_hop_ = my_id_;
        cmh->next_hop_ = nb->id_;

        send(p, 0);
    }
}

// -------------------------------------- //
void GPSRAgent::dumpEnergy() {
    dumpEnergy("Energy.tr");
}

void
GPSRAgent::dumpEnergy(char *filename) {
    if (node_->energy_model()) {
        FILE *fp = fopen(filename, "a+");
        fprintf(fp, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", my_id_, this->x_, this->y_,
                node_->energy_model()->energy(),
                node_->energy_model()->off_time(),
                node_->energy_model()->et(),
                node_->energy_model()->er(),
                node_->energy_model()->ei(),
                node_->energy_model()->es()
        );
        fclose(fp);
    }
}

void
GPSRAgent::dumpNeighbor() {
    FILE *fp = fopen("Neighbors.tr", "a+");
//    fprintf(fp, "%f\t%f\n", x_, y_);
    fprintf(fp, "%d	%f	%f	%f	", this->my_id_, this->x_, this->y_, node_->energy_model()->off_time());
    for (node *temp = neighbor_list_; temp; temp = temp->next_) {
        fprintf(fp, "%d,", temp->id_);
    }
    fprintf(fp, "\n");

    fclose(fp);
}

void GPSRAgent::dumpEnergyByTime() {
    if (node_->energy_model()) {
        FILE *fp = fopen("EnergyByTime.tr", "a+");
        fprintf(fp, "%f\t%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", NOW, my_id_, this->x_, this->y_,
                node_->energy_model()->energy(),
                node_->energy_model()->et(),
                node_->energy_model()->er(),
                node_->energy_model()->ei(),
                node_->energy_model()->es()
        );
        fclose(fp);
    }
}

void GPSRAgent::dumpPktOverhead() {
    FILE *fp = fopen("PacketOverhead.tr", "a+");
    fprintf(fp, "%d\t%d\t%d\t%d\t%d\n", hello_counter_, boundhole_pkt_counter_, HBA_counter_, HCI_counter_,
            data_pkt_counter_);
    fclose(fp);
}

void GPSRAgent::dumpRedundantNode(int d) {
    FILE *fp = fopen("RedundantNode.tr", "a+");
    int num = 0;
    for (node *temp = neighbor_list_; temp; temp = temp->next_) {
        num++;
    }
    fprintf(fp, "%d\t%d\n", d, num - 1);
    fclose(fp);
}