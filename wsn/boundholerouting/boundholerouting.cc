#include <packet.h>
#include <wsn/boundhole/boundhole.h>
#include "boundholerouting_packet.h"
#include "boundholerouting.h"

int hdr_boundholerouting::offset_;

static class BOUNDHOLEROUTINGHeaderClass : public PacketHeaderClass {
public:
    BOUNDHOLEROUTINGHeaderClass() : PacketHeaderClass("PacketHeader/BOUNDHOLEROUTING", sizeof(hdr_boundholerouting)) {
        bind_offset(&hdr_boundholerouting::offset_);
    }
} class_boundholeroutingshdr;

static class BOUNDHOLEROUTINGAgentClass : public TclClass {
public:
    BOUNDHOLEROUTINGAgentClass() : TclClass("Agent/BOUNDHOLEROUTING") { }

    TclObject *create(int, const char *const *) {
        return (new BOUNDHOLEROUTINGAgent());
    }
} class_boundholerouting;


BOUNDHOLEROUTINGAgent::BOUNDHOLEROUTINGAgent() : BoundHoleAgent() {
    hole_list_ = NULL;
}


void BOUNDHOLEROUTINGAgent::recv(Packet *p, Handler *h) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_ip *iph = HDR_IP(p);

    if (cmh->ptype() == PT_CBR) {
        if (iph->saddr() == my_id_)                // a packet generated by myself
        {
            if (cmh->num_forwards() == 0)        // a new packet
            {
                sendData(p);
            }
        }
        if (iph->ttl_-- <= 0) {
            drop(p, DROP_RTR_TTL);
            return;
        }
        recvData(p);
    }
    else
        BoundHoleAgent::recv(p, h);
}

int BOUNDHOLEROUTINGAgent::command(int argc, const char *const *argv) {
    if (argc == 2) {
        if (strcasecmp(argv[1], "routing") == 0) {
            return TCL_OK;
        }
    }
    return BoundHoleAgent::command(argc, argv);
}

void BOUNDHOLEROUTINGAgent::createHole(Packet *p) {
    polygonHole *h = createPolygonHole(p);
    h->circleNodeList();
    h->next_ = hole_list_;
    hole_list_ = h;
}

void BOUNDHOLEROUTINGAgent::sendData(Packet *p) {
    hdr_cmn *cmh = HDR_CMN(p);
    hdr_ip *iph = HDR_IP(p);
    struct hdr_boundholerouting *hbr = HDR_BOUNDHOLEROUTING(p);

    cmh->size() += IP_HDR_LEN + hbr->size();
    cmh->direction_ = hdr_cmn::DOWN;
    cmh->addr_type() = NS_AF_INET;

    hbr->dest_ = *dest;
    hbr->daddr_ = iph->daddr();
    hbr->type_ = BOUNDHOLE_GREEDY;
    hbr->distance_ = G::distance(*this, *dest);

    iph->saddr() = my_id_;
    iph->daddr() = -1;
    iph->ttl_ = 4 * IP_DEF_TTL;
}

void BOUNDHOLEROUTINGAgent::recvData(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_boundholerouting *hbr = HDR_BOUNDHOLEROUTING(p);

    if (cmh->direction() == hdr_cmn::UP && hbr->daddr_ == my_id_) {
        port_dmux_->recv(p, 0);
        return;
    }
    else {
        node *nb;
        switch(hbr->type_) {
            case BOUNDHOLE_GREEDY:
                nb = getNeighborByGreedy(hbr->dest_, *this);
                if (nb) {
                    hbr->distance_ = G::distance(*this, hbr->dest_);
                } else {
                    nb = getNextHopByBoundHole(*this);
                    if (nb == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    } else {
                        hbr->type_ = BOUNDHOLE_BOUNDHOLE;
                    }
                }
                break;
            case BOUNDHOLE_BOUNDHOLE:
                if(hbr->distance_ > G::distance(*this, hbr->dest_)) {
                    nb = getNeighborByGreedy(hbr->dest_, *this);
                    if (nb) {
                        hbr->type_ = BOUNDHOLE_GREEDY;
                        hbr->distance_ = G::distance(*this, hbr->dest_);
                    } else {
                        nb = getNextHopByBoundHole(*this);
                        if (nb == NULL) {
                            drop(p, DROP_RTR_NO_ROUTE);
                            return;
                        }
                    }
                } else {
                    nb = getNextHopByBoundHole(*this);
                    if (nb == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }
                }
                break;
            default:
                drop(p, "UnknownType");
                return;
        }

        cmh->direction_ = hdr_cmn::DOWN;
        cmh->last_hop_ = my_id_;
        cmh->next_hop_ = nb->id_;

        send(p, 0);
    }
}

node *BOUNDHOLEROUTINGAgent::getNextHopByBoundHole(Point p) {
    for (struct node *ntemp = hole_list_->node_list_->next_; ntemp != hole_list_->node_list_; ntemp = ntemp->next_) {
        if (p == *ntemp) {
            return ntemp->next_;
        }
    }
    return NULL;
}