#include <bits/ios_base.h>
#include <X11/Xutil.h>
#include "elbar.h"
#include "elbar_packet.h"

int hdr_elbar_gridonline::offset_;

static class ElbarGridOnlineHeaderClass : public PacketHeaderClass 
{
	public:
		ElbarGridOnlineHeaderClass() : PacketHeaderClass("PacketHeader/ELBARGRIDONLINE", sizeof(hdr_elbar_gridonline))
		{
			bind_offset(&hdr_elbar_gridonline::offset_);
		}
} class_elbargridonlinehdr;

static class ElbarGridOnlineAgentClass : public TclClass
{
	public:
		ElbarGridOnlineAgentClass() : TclClass("Agent/ELBARGRIDONLINE") {}
		TclObject *create(int argc, const char*const* argv) 
		{
			return (new ElbarGridOnlineAgent());
		}
} class_elbargridonline;


/**
 * Agent implementation
 */
ElbarGridOnlineAgent::ElbarGridOnlineAgent() : GridOnlineAgent()
{
    this->alpha_max_ = M_PI * 2 / 3;
    this->alpha_min_ = M_PI / 3;

    routing_mode_ = holeAvoidingProb();
}

int
ElbarGridOnlineAgent::command(int argc, const char*const* argv)
{
    if (argc == 2){
        if (strcasecmp(argv[1], "routing") == 0){
            // routing use elbar algorithm
            routing();
            return TCL_OK;
        }
    }

    return GridOnlineAgent::command(argc,argv);
}

// handle the receive packet just of type PT_ELBARGRIDONLINE
void
ElbarGridOnlineAgent::recv(Packet *p, Handler *h)
{
    hdr_cmn *cmh = HDR_CMN(p);
    hdr_elbar_gridonline *egh = HDR_ELBAR_GRID(<#p#>);

    switch (cmh->ptype())
    {
        case PT_HELLO:
            GPSRAgent::recv(p, h);
            break;

        case PT_ELBARGRIDONLINE: {
            if(egh->type_ == ELBAR_BOUNDHOLE) {
                recvBoundHole(p);
            }

            break;
        };

        default:
            drop(p, " UnknowType");
            break;
    }
}

/*------------------------------ Routing --------------------------*/
/*
 * Hole covering parallelogram determination
 */
bool ElbarGridOnlineAgent::detectParallelogram(){
    struct polygonHole*tmp;
    struct node* item;

    struct node* ai;
    struct node* aj;

    struct node a;
    struct node b;
    struct node c;

    struct node* vi;
    struct node* vj;

    double hi;
    double hj;
    double h;

    Line li;
    Line lj;

    double angle;
    /*// detect view angle
    for (tmp = hole_list_; tmp != NULL; tmp = tmp->next_){
        item = tmp->node_list_;
        struct node* left = tmp->node_list_;
        struct node* right = tmp->node_list_;
        if (item != NULL && item->next_ != NULL) {
            item = item->next_;
            do {
                Angle a = G::rawAngle(left, this, item, this);
                if (a < 0) left = item;
                a = G::rawAngle(right, this, item, this);
                if (a > 0) right = item;
                item = item->next_;
            } while (item && item != tmp->node_list_);
        }
    }*/

    // detect view angle
    ai = hole_list_->node_list_;
    aj = hole_list_->node_list_;
    item = tmp->node_list_;
    for(tmp = hole_list_; tmp != NULL; tmp = tmp->next_)
    {
        if(G::directedAngle(ai, this, item) > 0)
            ai = item;
        if(G::directedAngle(aj, this, item) < 0)
            aj = item;
        item = item->next_;
    }

    // detect parallelogram
    vi = hole_list_->node_list_;
    vj = hole_list_->node_list_;
    hi = 0;
    hj = 0;
    item = tmp->node_list_; // A(k)

    for(tmp = hole_list_; tmp != NULL; tmp = tmp->next_)
    {
        h = G::distance(item->x_, item->y_, this->x_, this->y_, ai->x_, ai->y_);
        if(h > hi) {
            hi = h;
            vi = item;
        }
        h = G::distance(item->x_, item->y_, this->x_, this->y_, aj->x_, aj->y_);
        if(h > hj) {
            hj = h;
            vj = item;
        }
        item = item->next_;
    }

    li = G::parallel_line(vi, G::line(this, ai));
    lj = G::parallel_line(vj, G::line(this, aj));

    if(!G::intersection(lj, G::line(this, ai), a) ||
        !G::intersection(li, G::line(this, aj), c) ||
        !G::intersection(li, lj, b))
        return false;

    /*
    save Hole information into local memory if this node is in region 2
     */
    angle = G::angle(ai, this, aj);
    region_ = regionDetermine(angle);
    if(REGION_2 == region_) {
        this->parallelogram_->a_ = a;
        this->parallelogram_->b_ = b;
        this->parallelogram_->c_ = c;
        this->parallelogram_->p_.x_ =this->x_;
        this->parallelogram_->p_.y_ = this->y_;
        this->alpha_ = angle;
    }

    return true;
}

RoutingMode ElbarGridOnlineAgent::holeAvoidingProb() {
    srand(time(NULL));
    if(rand() % 2 == 0)
        return HOLE_AWARE_MODE;
    return GREEDY_MODE;
}

Elbar_Region ElbarGridOnlineAgent::regionDetermine(double angle) {
    if(angle < alpha_min_) return REGION_3;
    if(angle > alpha_max_) return REGION_1;
    return REGION_2;
}

/*
 * Hole bypass routing
 * TODO: elbar routing algorithm implemtation
 */
void ElbarGridOnlineAgent::routing(Packet *p) {
    struct hdr_cmn*				    cmh = HDR_CMN(p);
    struct hdr_ip*				    iph = HDR_IP(p);
    struct hdr_elbar_gridonline*    egh = HDR_ELBAR_GRID(p);

    Point* destionantion;
    Angle alpha;
    Point anchor_point;
    RoutingMode routing_mode;

    if(region_ == REGION_3 || region_ == REGION_1) {
        // greedy mode
        egh->anchor_point_ = NULL;
        egh->forwarding_mode_ = GREEDY_MODE;
        node* nexthop = getNeighborByGreedy(*dest);
        if(nexthop == NULL) {
            drop(p, DROP_RTR_NO_ROUTE);
            return;
        }
        cmh->direction() = hdr_cmn::DOWN;
        cmh->addr_type() = NS_AF_INET;
        cmh->last_hop_ = my_id_;
        cmh->next_hop_ = nexthop->id_;
        send(p, 0);
    }
    else if(region_ == REGION_2) { // elbar routing
        destionantion = dest;
        alpha = this->alpha_;
        anchor_point = egh->anchor_point_;
        routing_mode = egh->forwarding_mode_;

        if(dest->x_ || dest->y_) {
            if (cmh->direction() == hdr_cmn::UP	&&
                    (this->x_ == destionantion->x_ && this->y_ == destionantion->y_))	// up to destination
            {
                dumpHopcount(p);
                port_dmux_->recv(p, 0);
                return;
            }
            if(routing_mode == HOLE_AWARE_MODE) {
                if(G::directedAngle(destionantion, this, &(parallelogram_->a_)) * G::directedAngle(destionantion, this, &(parallelogram_->c_)) >= 0)
                { // alpha does not contain D
                    egh->forwarding_mode_ = GREEDY_MODE;
                    node* nexthop = getNeighborByGreedy(*destionantion);
                    if(nexthop == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }
                    cmh->direction() = hdr_cmn::DOWN;
                    cmh->addr_type() = NS_AF_INET;
                    cmh->last_hop_ = my_id_;
                    cmh->next_hop_ = nexthop->id_;
                    send(p, 0);
                }
                else {
                    node* nexthop = getNeighborByGreedy(anchor_point);
                    if(nexthop == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }
                    cmh->direction() = hdr_cmn::DOWN;
                    cmh->addr_type() = NS_AF_INET;
                    cmh->last_hop_ = my_id_;
                    cmh->next_hop_ = nexthop->id_;
                    send(p, 0);
                }
            }
            else {
                if(alpha_ != NULL &&
                        G::directedAngle(destionantion, this, &(parallelogram_->a_)) * G::directedAngle(destionantion, this, &(parallelogram_->c_)) < 0) {
                    // alpha contains D
                    if(routing_mode_ == HOLE_AWARE_MODE) {

                        Line cd = G::line(parallelogram_->c_, destionantion);
                        Line ad = G::line(parallelogram_->a_, destionantion);
                        if(G::distance(parallelogram_->p_, parallelogram_->c_ )<=
                        G::distance(parallelogram_->p_, parallelogram_->a_)) {
                            // pc <= pa
                            if(G::directedAngle(destionantion, &(parallelogram_->c_), &(parallelogram_->p_)) *
                                    G::directedAngle(destionantion, &(parallelogram_->c_), &(parallelogram_->b_))
                                    >= 0) { // cd does not intersect with the hole
                                egh->anchor_point_ = parallelogram_->c_;
                            }
                            else {
                                egh->anchor_point_ = parallelogram_->a_;
                            }
                        }
                        else {
                            if(G::directedAngle(destionantion, &(parallelogram_->a_), &(parallelogram_->p_)) *
                                    G::directedAngle(destionantion, &(parallelogram_->a_), &(parallelogram_->b_))
                                    >= 0) {
                                egh->anchor_point_ = parallelogram_->a_;
                            }
                            else {
                                egh->anchor_point_ = parallelogram_->c_;
                            }
                        }

                        egh->forwarding_mode_ = HOLE_AWARE_MODE;
                        node* nexthop = getNeighborByGreedy(anchor_point);
                        if(nexthop == NULL) {
                            drop(p, DROP_RTR_NO_ROUTE);
                            return;
                        }
                        cmh->direction() = hdr_cmn::DOWN;
                        cmh->addr_type() = NS_AF_INET;
                        cmh->last_hop_ = my_id_;
                        cmh->next_hop_ = nexthop->id_;
                        send(p, 0);
                    }
                    else {
                        node* nexthop = getNeighborByGreedy(*destionantion);
                        if(nexthop == NULL) {
                            drop(p, DROP_RTR_NO_ROUTE);
                            return;
                        }
                        cmh->direction() = hdr_cmn::DOWN;
                        cmh->addr_type() = NS_AF_INET;
                        cmh->last_hop_ = my_id_;
                        cmh->next_hop_ = nexthop->id_;
                        send(p, 0);
                    }
                }
                else {
                    node* nexthop = getNeighborByGreedy(*destionantion);
                    if(nexthop == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }
                    cmh->direction() = hdr_cmn::DOWN;
                    cmh->addr_type() = NS_AF_INET;
                    cmh->last_hop_ = my_id_;
                    cmh->next_hop_ = nexthop->id_;
                    send(p, 0);
                }
            }
        }
    }
}

/*---------------------- BoundHole --------------------------------*/
/**
* TODO:
*/
void ElbarGridOnlineAgent::recvBoundHole(Packet *p) {

    detectParallelogram();

    if(region_ == REGION_3) { // stop broadcast hole core information
        drop(p, "out_of_region_2");
    }
    else {
        GridOnlineAgent::recvBoundHole(p);
    }
}

/**
* TODO: do what?
*/
void ElbarGridOnlineAgent::sendBoundHole() {
    GridOnlineAgent::sendBoundHole();
}
