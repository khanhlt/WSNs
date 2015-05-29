#include "elbar.h"
#include "elbar_packet.h"
#include "elbar_packet_data.h"

int hdr_elbar_grid::offset_;

static class ElbarGridOfflineHeaderClass : public PacketHeaderClass {
public:
    ElbarGridOfflineHeaderClass() : PacketHeaderClass("PacketHeader/ELBARGRIDOFFLINE", sizeof(hdr_elbar_grid)) {
        bind_offset(&hdr_elbar_grid::offset_);
    }
} class_elbargridofflinehdr;

static class ElbarGridOfflineAgentClass : public TclClass {
public:
    ElbarGridOfflineAgentClass() : TclClass("Agent/ELBARGRIDOFFLINE") {
    }

    TclObject *create(int argc, const char *const *argv) {
        return (new ElbarGridOfflineAgent());
    }
} class_elbargridoffline;

/**
* Agent implementation
*/
ElbarGridOfflineAgent::ElbarGridOfflineAgent()
        : GridOfflineAgent() {
    this->alpha_max_ = M_PI * 2 / 3;
    this->alpha_min_ = M_PI / 3;

    hole_list_ = NULL;
    parallelogram_ = NULL;
    alpha_ = 0;
}

char const *ElbarGridOfflineAgent::getAgentName() {
    return "ElbarGirdOffline";
}

int
ElbarGridOfflineAgent::command(int argc, const char *const *argv) {
    if (argc == 2) {
        if (strcasecmp(argv[1], "broadcast") == 0) {
            broadcastHci();
            return TCL_OK;
        }
    }

    return GridOfflineAgent::command(argc, argv);
}

// handle the receive packet just of type PT_ELBARGRID
void
ElbarGridOfflineAgent::recv(Packet *p, Handler *h) {
    hdr_cmn *cmh = HDR_CMN(p);
    hdr_ip *iph = HDR_IP(p);
    switch (cmh->ptype()) {
        case PT_CBR:
            if (iph->saddr() == my_id_)                // a packet generated by myself
            {
                if (cmh->num_forwards() == 0)        // a new packet
                {
                    configDataPacket(p); // config packet before send
                }
                else    //(cmh->num_forwards() > 0)	// routing loop -> drop
                {
                    drop(p, DROP_RTR_ROUTE_LOOP);
                    return;
                }
            }
            if (iph->ttl_-- <= 0) {
                drop(p, DROP_RTR_TTL);
                return;
            }

            recvData(p);
            break;
        case PT_ELBARGRID:
            recvElbar(p);
            break;
        default:
            GridOfflineAgent::recv(p, h);
            break;
    }
}

/*------------------------------ Recv -----------------------------*/
void ElbarGridOfflineAgent::recvElbar(Packet *p) {
    hdr_elbar_grid *egh = HDR_ELBAR_GRID(p);

    switch (egh->type_) {
        case ELBAR_BROADCAST:
            recvHci(p);
            break;
        case ELBAR_DATA:
            routing(p);
            break;
        default:
            drop(p, "UnknowType");
            break;
    }
}

// handle recv packet/ send new packet
void ElbarGridOfflineAgent::recvData(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_elbar_grid *egh = HDR_ELBAR_GRID(p);

    if (cmh->direction_ == hdr_cmn::UP && egh->daddr == my_id_) { // packet reach destination
        printf("[Debug] %d - Received\n", my_id_);
        port_dmux_->recv(p, 0);
        return;
    } else {// send new packet or routing recv packet
        routing(p);
        return;
    }
}

// add Elbar header to cmn header
void ElbarGridOfflineAgent::configDataPacket(Packet *p) {
    hdr_cmn *cmh = HDR_CMN(p);
    hdr_ip *iph = HDR_IP(p);
    hdr_elbar_grid *egh = HDR_ELBAR_GRID(p);

    cmh->size() += IP_HDR_LEN + egh->size();

    cmh->direction() = hdr_cmn::DOWN;

    egh->type_ = ELBAR_DATA;
    egh->daddr = iph->daddr(); // save destionation address
    egh->forwarding_mode_ = GREEDY_MODE;
    egh->destination_ = *(this->dest); // save destionation node and broadcast

    egh->anchor_point_.x_ = -1;
    egh->anchor_point_.y_ = -1;

    iph->saddr() = my_id_;
    iph->daddr() = -1;
    iph->ttl_ = 100;

    sendGPSR(p);
}

/*------------------------------ Routing --------------------------*/
/*
 * Hole covering parallelogram determination & region determination
 */
void ElbarGridOfflineAgent::detectParallelogram() {
    struct polygonHole *tmp;
    struct node *ai;
    struct node *aj;

    struct node *vi;
    struct node *vj;
    struct node *item;

    double angle;

    for (tmp = hole_list_; tmp; tmp = tmp->next_) {
        struct node a;
        struct node b;
        struct node c;

        double hi;
        double hj;
        double h;

        Line li;
        Line lj;

        // check if node is inside grid
        if (!isPointInsidePolygon(this, tmp->node_list_)) {
            // detect view angle
            ai = tmp->node_list_;
            aj = tmp->node_list_;
            item = tmp->node_list_;

            do {
                if (G::directedAngle(ai, this, item) > 0) {
                    ai = item;
                }
                if (G::directedAngle(aj, this, item) < 0)
                    aj = item;
                item = item->next_;
            } while (item && item->next_ != tmp->node_list_);


            // detect parallelogram
            vi = tmp->node_list_;
            vj = tmp->node_list_;
            hi = 0;
            hj = 0;

            item = tmp->node_list_;

            do {
                h = G::distance(item->x_, item->y_, this->x_, this->y_, ai->x_, ai->y_);
                if (h > hi) {
                    hi = h;
                    vi = item;
                }
                h = G::distance(item->x_, item->y_, this->x_, this->y_, aj->x_, aj->y_);
                if (h > hj) {
                    hj = h;
                    vj = item;
                }
                item = item->next_;
            } while (item && item->next_ != tmp->node_list_);

            li = G::parallel_line(vi, G::line(this, ai));
            lj = G::parallel_line(vj, G::line(this, aj));

            if (!G::intersection(lj, G::line(this, ai), &a) ||
                    !G::intersection(li, G::line(this, aj), &c) ||
                    !G::intersection(li, lj, &b)) {
                fprintf(stderr, "%d - fail detect parallelogram\n", my_id_);
                continue;
            }

            /*
        save Hole information into local memory if this node is in region 2
         */
            angle = G::directedAngle(aj, this, ai);
            angle = angle > 0 ? angle : angle + M_PI; //change angle from rang -180;180 to 0;360

            region_ = regionDetermine(angle);
            if (REGION_2 == region_ || REGION_1 == region_) {
                this->parallelogram_ = new parallelogram();
                this->parallelogram_->a_ = a;
                this->parallelogram_->b_ = b;
                this->parallelogram_->c_ = c;
                this->parallelogram_->p_.x_ = this->x_;
                this->parallelogram_->p_.y_ = this->y_;
            }

            alpha_ = angle;
        } else {
            alpha_ = -1;
            parallelogram_ = NULL;
            region_ = REGION_1;
        }
    }
}

int ElbarGridOfflineAgent::holeAvoidingProb() {
    RNG rand_;
    if (rand_.uniform(0, 1) < alpha_ / M_PI) {
        return HOLE_AWARE_MODE;
    } else {
        return GREEDY_MODE;
    }
}

Elbar_Region ElbarGridOfflineAgent::regionDetermine(double angle) {
    if (angle < alpha_min_) return REGION_3;
    if (angle > alpha_max_) return REGION_1;
    return REGION_2;
}

/*
 * Hole bypass routing
 */
void ElbarGridOfflineAgent::routing(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_elbar_grid *egh = HDR_ELBAR_GRID(p);
    struct hdr_ip *iph = HDR_IP(p);

    Point *destination = &(egh->destination_);
    Point *anchor_point = &(egh->anchor_point_);
    int routing_mode = egh->forwarding_mode_;

    // forward by GPSR when have no info about hole
    if (!hole_list_){
        node *nexthop = recvGPSR(p, *destination);
        if (nexthop == NULL) {
            drop(p, DROP_RTR_NO_ROUTE);
            return;
        }
        sendPackageToHop(p, nexthop);
        return;
    }

    if (cmh->uid_ == 1739){
        if (my_id_ == 114){
            int i = 0;
        }
        int k = 0;
    }

    if (region_ == REGION_2) {

        int i;
        if (cmh->uid_ == 1728){
            if (my_id_ == 331){
                int j = 0;
            }
            int i = my_id_;
        }
        if (routing_mode == HOLE_AWARE_MODE) { // if hole aware mode
            if (!isAlphaContainsPoint(&(parallelogram_->a_), this, &(parallelogram_->c_), destination)) {
                //if (!isBetweenAngle(destination, &(parallelogram_->a_), this, &(parallelogram_->c_))) {
                // alpha does not contain D

                // routing by greedy
                egh->forwarding_mode_ = GREEDY_MODE;
                // set nexthop to neighbor being closest to D
                node *nexthop = recvGPSR(p, *destination);
                if (nexthop == NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return;
                }

                // reset anchor point
                egh->anchor_point_.x_ = -1;
                egh->anchor_point_.y_ = -1;
                sendPackageToHop(p, nexthop);
            }
            else {
                // alpha contains D
                // set nexthop to neighbor being closest to L
                node *nexthop = recvGPSR(p, *anchor_point);
                if (nexthop == NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return;
                }
                sendPackageToHop(p, nexthop);
            }
        }
        else { // is in greedy mode
            if (alpha_ && isAlphaContainsPoint(&(parallelogram_->a_), this, &(parallelogram_->c_), destination)) {
                //if (isBetweenAngle(destination, &(parallelogram_->a_), this, &(parallelogram_->c_))) {
                // alpha contains D

                routing_mode_ = holeAvoidingProb();
                if (routing_mode_ == HOLE_AWARE_MODE) {
                    if (G::distance(parallelogram_->p_, parallelogram_->c_) <=
                            G::distance(parallelogram_->p_, parallelogram_->a_)) {
                        // pc <= pa
                        if (!isIntersectWithHole(&(parallelogram_->c_), destination, hole_list_->node_list_)) {
                            egh->anchor_point_ = (parallelogram_->c_);
                        }
                        else {
                            egh->anchor_point_ = (parallelogram_->a_);
                        }
                    }
                    else {
                        if (!isIntersectWithHole(&(parallelogram_->a_), destination, hole_list_->node_list_)) {
                            egh->anchor_point_ = (parallelogram_->a_);
                        }
                        else {
                            egh->anchor_point_ = (parallelogram_->c_);
                        }
                    }

                    egh->forwarding_mode_ = HOLE_AWARE_MODE;
                    node *nexthop = recvGPSR(p, *anchor_point);
                    if (nexthop == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }

                    dumpAnchorPoint(cmh->uid_, anchor_point);
                    sendPackageToHop(p, nexthop);
                }
                else {
                    node *nexthop = recvGPSR(p, *destination);
                    if (nexthop == NULL) {
                        drop(p, DROP_RTR_NO_ROUTE);
                        return;
                    }

                    sendPackageToHop(p, nexthop);
                }
            }
            else { // alpha does not contains D
                node *nexthop = recvGPSR(p, *destination);
                if (nexthop == NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return;
                }
                // reset anchor point
                egh->anchor_point_.x_ = -1;
                egh->anchor_point_.y_ = -1;

                sendPackageToHop(p, nexthop);
            }
        }
    } else {
        if (REGION_1 == region_){
            int i = 0;
        }
        node *nexthop;
        if (anchor_point->x_ != -1 && anchor_point->y_ != -1){
            // forward to anchor point when anchor point is set
            nexthop = recvGPSR(p, *anchor_point);
        } else {
            nexthop = recvGPSR(p, *destination);
        }

        sendPackageToHop(p, nexthop);
    }
}

/*
* send package down to nexthop
*/
void ElbarGridOfflineAgent::sendPackageToHop(Packet *p, node *nexthop){
    struct hdr_cmn *cmh = HDR_CMN(p);

    cmh->direction() = hdr_cmn::DOWN;
    cmh->addr_type() = NS_AF_INET;
    cmh->last_hop_ = my_id_;
    cmh->next_hop_ = nexthop->id_;
    send(p, 0);
}

/*---------------------- Broacast HCI --------------------------------*/
void ElbarGridOfflineAgent::broadcastHci() {

    Packet *p = NULL;
    ElbarGridOfflinePacketData *payload;
    hdr_cmn *cmh;
    hdr_ip *iph;
    hdr_elbar_grid *egh;

    polygonHole *tmp;
//
    if (hole_list_ == NULL)
        return;

//    detect parallelogram
    detectParallelogram();

    p = allocpkt();
    payload = new ElbarGridOfflinePacketData();
    for (tmp = hole_list_; tmp != NULL; tmp = tmp->next_) {
        node *item = tmp->node_list_;
        do {
            payload->add_data(item->x_, item->y_);
            item = item->next_;
        } while (item && item->next_ != tmp->node_list_);
    }
    p->setdata(payload);

    cmh = HDR_CMN(p);
    iph = HDR_IP(p);
    egh = HDR_ELBAR_GRID(p);

    cmh->ptype() = PT_ELBARGRID;
    cmh->direction() = hdr_cmn::DOWN;
    cmh->next_hop_ = IP_BROADCAST;
    cmh->last_hop_ = my_id_;
    cmh->addr_type_ = NS_AF_INET;
    cmh->size() += IP_HDR_LEN + egh->size();

    iph->daddr() = IP_BROADCAST;
    iph->saddr() = my_id_;
    iph->sport() = RT_PORT;
    iph->dport() = RT_PORT;
    iph->ttl_ = limit_boundhole_hop_;

    egh->forwarding_mode_ = GREEDY_MODE;
    egh->type_ = ELBAR_BROADCAST;

    send(p, 0);
}

void ElbarGridOfflineAgent::sendElbar(Packet *p) {
    hdr_cmn *cmh = HDR_CMN(p);
    hdr_ip *iph = HDR_IP(p);

    cmh->direction() = hdr_cmn::DOWN;
    cmh->last_hop_ = my_id_;

    iph->ttl_--;

    send(p, 0);
}

void ElbarGridOfflineAgent::recvHci(Packet *p) {
    struct hdr_ip *iph = HDR_IP(p);

    // if the hci packet has came back to the initial node
    if (iph->saddr() == my_id_) {
        drop(p, "ElbarGridOfflineLoopHCI");
        return;
    }

    if (iph->ttl_-- <= 0) {
        drop(p, DROP_RTR_TTL);
        return;
    }

    // check if is really receive this hole's information
    for (polygonHole *h = hole_list_; h; h = h->next_) {
        if (h->hole_id_ == iph->saddr())    // already received
        {
            drop(p, "HciReceived");
            return;
        }
    }

    // create Grid if not set
    createGrid(p);
    // determine region & parallelogram
    detectParallelogram();

    if (REGION_3 == region_) {
        drop(p, "ElbarGridOffline_IsInRegion3");
    }
    else if (REGION_1 == region_ || REGION_2 == region_) {
        sendElbar(p);
    }
}

void ElbarGridOfflineAgent::createGrid(Packet *p) {
    struct hdr_ip *iph = HDR_IP(p);

    // create hole core information
    polygonHole *hole_item = new polygonHole();
    hole_item->node_list_ = NULL;
    hole_item->hole_id_ = iph->saddr();

    // add node info to hole item
    ElbarGridOfflinePacketData *data = (ElbarGridOfflinePacketData *) p->userdata();
    data->dump();

    node *head = NULL;
    for (int i = 1; i <= data->size(); i++) {
        node n = data->get_data(i);
        node *item = new node();
        item->x_ = n.x_;
        item->y_ = n.y_;
        item->next_ = head;
        head = item;
    }

    hole_item->node_list_ = head;
    hole_item->next_ = hole_list_;
    hole_list_ = hole_item;
}

/******************* GPRS ***********************/
void ElbarGridOfflineAgent::sendGPSR(Packet *p) {
    struct hdr_elbar_grid *egh = HDR_ELBAR_GRID(p);
    egh->gprs_type_ = GPSR_GPSR;
}

node *ElbarGridOfflineAgent::recvGPSR(Packet *p, Point destionation) {
    struct hdr_elbar_grid *egh = HDR_ELBAR_GRID(p);

    node *nb = NULL;

    switch (egh->gprs_type_) {
        case GPSR_GPSR:
            nb = this->getNeighborByGreedy(destionation, *this);

            if (nb == NULL) {
                nb = getNeighborByPerimeter(destionation);

                if (nb == NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return NULL;
                }
                else {
                    egh->gprs_type_ = GPSR_PERIME;
                    egh->peri_ = *this;
                }
            }
            break;

        case GPSR_PERIME:
            // try to get back to greedy mode
            nb = this->getNeighborByGreedy(destionation, egh->peri_);
            if (nb) {
                egh->gprs_type_ = GPSR_GPSR;
            }
            else {
                nb = getNeighborByPerimeter(egh->prev_);
                if (nb == NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return NULL;
                }
            }
            break;

        default:
            drop(p, " UnknowType");
            return NULL;
    }

    egh->prev_ = *this;

    return nb;
}

/*---------------------- Dump --------------------------------*/
void ElbarGridOfflineAgent::initTraceFile() {
    FILE *fp;
    fp = fopen("Area.tr", "w");
    fclose(fp);
    fp = fopen("GridOffline.tr", "w");
    fclose(fp);
    fp = fopen("Neighbors.tr", "w");
    fclose(fp);
    fp = fopen("AnchorPoint.tr", "w");
    fclose(fp);
//    fp = fopen("Parallelogram.tr", "w");
//    fclose(fp);
}

//void ElbarGridOfflineAgent::dumpParallelogram() {
//    FILE *fp = fopen("Parallelogram.tr", "a+");
//    fprintf(fp, "%f\t%f\n", this->parallelogram_->p_.x_, this->parallelogram_->p_.y_);
//    fprintf(fp, "%f\t%f\n", this->parallelogram_->a_.x_, this->parallelogram_->a_.y_);
//    fprintf(fp, "%f\t%f\n", this->parallelogram_->b_.x_, this->parallelogram_->b_.y_);
//    fprintf(fp, "%f\t%f\n", this->parallelogram_->c_.x_, this->parallelogram_->c_.y_);
//    fprintf(fp, "\n");
//    fclose(fp);
//}

//********** Math Helpers function ******************//
// convert to geo_math_helper library

bool ElbarGridOfflineAgent::isIntersectWithHole(Point *anchor, Point *dest, node *node_list) {

    node *tmp;

    for (tmp = node_list; tmp->next_ != NULL; tmp = tmp->next_) {
        if (G::is_intersect(anchor, dest, tmp, tmp->next_))
            return true;
    }
    return false;

}

bool ElbarGridOfflineAgent::isAlphaContainsPoint(Point *x, Point *o, Point *y, Point *d) {
    if (G::is_intersect(x, y, o, d)) { // OD intersects with XY
        return true;
    }
    else { // if D is inside XOY triangle
        return (isPointLiesInTriangle(d, x, o, y));
    }
}

bool ElbarGridOfflineAgent::isPointLiesInTriangle(Point *p, Point *p1, Point *p2, Point *p3) {
    // barycentric algorithm
    double alpha = ((p2->y_ - p3->y_) * (p->x_ - p3->x_) + (p3->x_ - p2->x_) * (p->y_ - p3->y_)) /
            ((p2->y_ - p3->y_) * (p1->x_ - p3->x_) + (p3->x_ - p2->x_) * (p1->y_ - p3->y_));
    double beta = ((p3->y_ - p1->y_) * (p->x_ - p3->x_) + (p1->x_ - p3->x_) * (p->y_ - p3->y_)) /
            ((p2->y_ - p3->y_) * (p1->x_ - p3->x_) + (p3->x_ - p2->x_) * (p1->y_ - p3->y_));
    double gamma = 1.0f - alpha - beta;

    return gamma >= 0 && alpha >= 0 && beta >= 0;
}

bool ElbarGridOfflineAgent::isPointInsidePolygon(Point *d, node *hole) {
    Point y;
    node *tmp;
    y.x_ = 0;
    y.y_ = d->y_;

    int greater_horizontal = 0;
    int less_horizontal = 0;
    Line dy = G::line(d, y);

    Point intersect;
    intersect.x_ = -1;
    intersect.y_ = -1;

    // count horizontal
    for (tmp = hole; tmp != NULL; tmp = tmp->next_) {
        if (tmp->next_ != NULL) {
            if (G::lineSegmentIntersection(tmp, tmp->next_, dy, intersect)) {
                if (intersect.x_ > d->x_) greater_horizontal++;
                else if (intersect.x_ < d->x_) less_horizontal++;
                else return true;
            }

        }
        else {
            if (G::lineSegmentIntersection(tmp, hole, dy, intersect)) {
                if (intersect.x_ > d->x_) greater_horizontal++;
                else if (intersect.x_ < d->x_) less_horizontal++;
                else return true;
            }

        }
    }

    return !(greater_horizontal % 2 == 0 && less_horizontal % 2 == 0);
}

bool ElbarGridOfflineAgent::isBetweenAngle(Point *pDes, Point *pNode, Point *pMid, Point *pNode1) {
    double a1 = G::directedAngle(pDes, pMid, pNode);
    double a2 = G::directedAngle(pDes, pMid, pNode1);
    double a3;

    if (a1 * a2 < 0) {
        a3 = G::directedAngle(pNode, pMid, pNode1);
        if (fabs(a1) + fabs(a2) - fabs(a3) < 0.000001) { // check if |a1| + |a2| = |a3|
            return true;
        }
    }
    return false;
}

void ElbarGridOfflineAgent::dumpAnchorPoint(int id, Point *pPoint) {
    FILE *fp = fopen("AnchorPoint.tr", "a+");
    fprintf(fp, "%d\t%f\t%f\n", id, pPoint->x_, pPoint->y_);
    fclose(fp);
}
