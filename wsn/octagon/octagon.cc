/*
 * octagon.cc
 *
 *  Last edited on Nov 14, 2013
 *  by Trong Nguyen
 */

/**
 * octagon in brief:
1. initial network setup
	- produce core polygon (8 vertices)
		1.1 boundhole
		1.2 construct core polygon
		1.3 broadcast hole information
			broadcast condition: (1) formula
=> region1: has hole information
=> region2: doesn’t have hole information

2. routing
routing: greedy until reach region1. transform to hole aware mode.
 		 greedy to virtual anchor points which is vertices of _packet-specific_ polygon.
3. note:
scale factor is the same for each CBR (source - dest)
point I (random point inside polygon) is random for each _packet_ (even same source - dest)
 *
 * source code analysis
 * after boundhole finished (1.1), run phase 1.2 & 1.3 immediately
 */

//#define _DEBUG	// allow dump result to file

#include "octagon.h"

#define fsin3pi8 1.0824

int hdr_octagon::offset_;

static class OctagonHeaderClass : public PacketHeaderClass {
public:
    OctagonHeaderClass() : PacketHeaderClass("PacketHeader/OCTAGON", sizeof(hdr_all_octagon)) {
        bind_offset(&hdr_octagon::offset_);
    }
} class_octagonhdr;

static class OctagonAgentClass : public TclClass {
public:
    OctagonAgentClass() : TclClass("Agent/OCTAGON") { }

    TclObject *create(int, const char *const *) {
        return (new OctagonAgent());
    }
} class_octagon;


// -------------------------------------- Agent -------------------------------------- //

OctagonAgent::OctagonAgent() : BoundHoleAgent(), broadcast_timer_(this) {
    octagonHole_list_ = NULL;
    scale_factor_ = -1;
    alpha_ = 0;
    ln_ = 0;
    region_ = REGION_2;

    bind("broadcast_rate_", &stretch_);

    // clear trace file
    FILE *fp;

    fp = fopen("BaseNode.tr", "w");
    fclose(fp);
    fp = fopen("ApproximateHole.tr", "w");
    fclose(fp);
    fp = fopen("BroadcastRegion.tr", "w");
    fclose(fp);
    fp = fopen("RoutingTable.tr", "w");
    fclose(fp);
    fp = fopen("DynamicScaleHole.tr", "w");
    fclose(fp);
}

void OctagonAgent::recv(Packet *p, Handler *h) {
    hdr_cmn *cmh = HDR_CMN(p);

    if (cmh->ptype() == PT_OCTAGON)
        recvBroadcast(p);
    else
        BoundHoleAgent::recv(p, h);
}

int OctagonAgent::command(int argc, const char *const *argv) {
    return BoundHoleAgent::command(argc, argv);
}

// --------------------- Approximate hole ------------------------- //

/**
 * huyvq: algorithm 1 implementation: Core polygon determination
 */
void OctagonAgent::createHole(Packet *p) {
    polygonHole *h = createPolygonHole(p);

    // create new octagonHole
    octagonHole *newHole = new octagonHole();
    newHole->hole_id_ = h->hole_id_;
    newHole->node_list_ = NULL;
    newHole->next_ = octagonHole_list_;
    octagonHole_list_ = newHole;

    /*
     * define 8 lines for new approximate hole
     *
     * 		    ____[2]_________
     * 		[02]	 			[21]______
     * 	   /		 					  \
     * 	[0] ----------------------------- [1]
     * 	   \____                       ___/
     * 			[30]____   ________[13]
     * 					[3]
     */

    node *n0 = NULL;
    node *n1 = NULL;
    node *n2 = NULL;
    node *n3 = NULL;

    Line bl;            // base line
    Line l0;            // line contain n0 and perpendicular with base line
    Line l1;            // line contain n1 and perpendicular with base line
    Line l2;            // line contain n2 and parallel with base line
    Line l3;            // line contain n3 and parallel with base line

    /*
     * huyvq: Select H p , H q as the two ends of the longest diagonal of the polygon.
     */
    // find couple node that have maximum distance - n0, n1
    newHole->d_ = 0;
    for (struct node *i = h->node_list_; i; i = i->next_) {
        for (struct node *j = i->next_; j; j = j->next_) {
            double dis = G::distance(i, j);
            if (newHole->d_ < dis) {
                newHole->d_ = dis;
                n0 = i;
                n1 = j;
            }
        }
    }

    bl = G::line(n0, n1); // bl = H(p)H(q)
    l0 = G::perpendicular_line(n0, bl);
    l1 = G::perpendicular_line(n1, bl);

    // cycle the node_list
    h->circleNodeList();

    /*
     * huyvq: choose H(j) and H(k)
     */
    // find n2 and n3 - with maximum distance from base line
    double mdis = 0;
    for (struct node *i = n0; i != n1->next_; i = i->next_) {
        double dis = G::distance(i, bl);
        if (dis > mdis) {
            mdis = dis;
            n2 = i;
        }
    }
    mdis = 0;
    for (struct node *i = n1; i != n0->next_; i = i->next_) {
        double dis = G::distance(i, bl);
        if (dis > mdis) {
            mdis = dis;
            n3 = i;
        }
    }

    // check if n2 and n3 in other side of base line of not
    if (G::position(n2, bl) * G::position(n3, bl) > 0) // n2 and n3 in same side of base line
    {
        if (G::distance(n2, bl) > G::distance(n3, bl)) {
            // re-find n3 in other side of bl
            double n2side = G::position(n2, bl);
            mdis = 0;
            for (struct node *i = n1; i != n0->next_; i = i->next_) {
                if (G::position(i, bl) * n2side <= 0) {
                    double dis = G::distance(i, bl);
                    if (dis > mdis) {
                        mdis = dis;
                        n3 = i;
                    }
                }
            }
        }
        else    // re-find n2 in other side of bl
        {
            double n3side = G::position(n3, bl);
            mdis = 0;
            for (struct node *i = n0; i != n1->next_; i = i->next_) {
                if (G::position(i, bl) * n3side <= 0) {
                    double dis = G::distance(i, bl);
                    if (dis > mdis) {
                        mdis = dis;
                        n2 = i;
                    }
                }
            }
        }
    }

    l2 = G::parallel_line(n2, bl);
    l3 = G::parallel_line(n3, bl);

    // ------------ calculate approximate home

    Point itsp; // intersect point (tmp)
    Line itsl;  // intersect line (tmp)
    double mc;

    // l02 intersection l0 and l2
    G::intersection(l0, l2, itsp);
    itsl = G::angle_bisector(n0, n1, itsp);
    mdis = 0;
    mc = 0;
    for (struct node *i = n0; i != n2->next_; i = i->next_) {
        itsl.c_ = -(itsl.a_ * i->x_ + itsl.b_ * i->y_);
        double dis = G::distance(n3, itsl);
        if (dis > mdis) {
            mdis = dis;
            mc = itsl.c_;
        }
    }
    itsl.c_ = mc;
    G::intersection(l0, itsl, itsp);
    addHoleNode(itsp);
    G::intersection(l2, itsl, itsp);
    addHoleNode(itsp);

    // l21 intersection l2 and l1
    G::intersection(l1, l2, itsp);
    itsl = G::angle_bisector(n1, n0, itsp);
    mdis = 0;
    mc = 0;
    for (struct node *i = n2; i != n1->next_; i = i->next_) {
        itsl.c_ = -(itsl.a_ * i->x_ + itsl.b_ * i->y_);
        double dis = G::distance(n3, itsl);
        if (dis > mdis) {
            mdis = dis;
            mc = itsl.c_;
        }
    }
    itsl.c_ = mc;
    G::intersection(l2, itsl, itsp);
    addHoleNode(itsp);
    G::intersection(l1, itsl, itsp);
    addHoleNode(itsp);

    // l13 intersection l1 and l3
    G::intersection(l1, l3, itsp);
    itsl = G::angle_bisector(n1, n0, itsp);
    mdis = 0;
    mc = 0;
    for (struct node *i = n1; i != n3->next_; i = i->next_) {
        itsl.c_ = -(itsl.a_ * i->x_ + itsl.b_ * i->y_);
        double dis = G::distance(n2, itsl);
        if (dis > mdis) {
            mdis = dis;
            mc = itsl.c_;
        }
    }
    itsl.c_ = mc;
    G::intersection(l1, itsl, itsp);
    addHoleNode(itsp);
    G::intersection(l3, itsl, itsp);
    addHoleNode(itsp);

    // l30 intersection l3 and l0
    G::intersection(l0, l3, itsp);
    itsl = G::angle_bisector(n0, n1, itsp);
    mdis = 0;
    mc = 0;
    for (struct node *i = n3; i != n0->next_; i = i->next_) {
        itsl.c_ = -(itsl.a_ * i->x_ + itsl.b_ * i->y_);
        double dis = G::distance(n2, itsl);
        if (dis > mdis) {
            mdis = dis;
            mc = itsl.c_;
        }
    }
    itsl.c_ = mc;
    G::intersection(l3, itsl, itsp);
    addHoleNode(itsp);
    G::intersection(l0, itsl, itsp);
    addHoleNode(itsp);

    // cycle the node_list
    node *temp = newHole->node_list_;
    while (temp->next_) temp = temp->next_;
    temp->next_ = newHole->node_list_;

    // calculate pc_
    newHole->pc_ = 0;
    node *i = newHole->node_list_;
    do {
        newHole->pc_ += G::distance(i, i->next_);
        i = i->next_;
    } while (i != newHole->node_list_);

    // calculate delta_
    newHole->delta_ = 0;
    i = h->node_list_;
    do {
        newHole->delta_ += G::distance(i, i->next_); // delta_ = perimeter of boundhole
        i = i->next_;
    } while (i != h->node_list_);

    newHole->delta_ = newHole->pc_ - newHole->delta_;

    region_ = REGION_1;

    // Broadcast hole information
    sendBroadcast(newHole);

    // dump
    dumpApproximateHole();
}

void OctagonAgent::addHoleNode(Point newPoint) {
    for (node *i = octagonHole_list_->node_list_; i; i = i->next_) {
        if (*i == newPoint) return;
    }
    node *newNode = new node();
    newNode->x_ = newPoint.x_;
    newNode->y_ = newPoint.y_;
    newNode->next_ = octagonHole_list_->node_list_;
    octagonHole_list_->node_list_ = newNode;
}

// --------------------- Broadcast hole identifier ---------------- //

void OctagonAgent::sendBroadcast(octagonHole *h) {
    Packet *p = allocpkt();
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_ip *iph = HDR_IP(p);
    struct hdr_octagon_ha *ehh = HDR_OCTAGON_HA(p);

    cmh->ptype() = PT_OCTAGON;
    cmh->next_hop_ = IP_BROADCAST;
    cmh->last_hop_ = my_id_;
    cmh->addr_type_ = NS_AF_INET;
    cmh->size() = IP_HDR_LEN + ehh->size();

    iph->daddr() = IP_BROADCAST;
    iph->saddr() = my_id_;
    iph->sport() = RT_PORT;
    iph->dport() = RT_PORT;
    iph->ttl_ = IP_DEF_TTL;

    ehh->id_ = h->hole_id_;
    ehh->pc_ = h->pc_;
    ehh->delta_ = h->delta_;
    ehh->d_ = h->d_;
    ehh->vertex_num_ = 0;

    node *i = h->node_list_;
    do {
        ehh->vertex[ehh->vertex_num_].x_ = i->x_;
        ehh->vertex[ehh->vertex_num_].y_ = i->y_;
        ehh->vertex_num_++;

        i = i->next_;
    }
    while (i != h->node_list_);

    send(p, 0);
}

void OctagonAgent::recvBroadcast(Packet *p) {
    struct hdr_octagon_ha *edh = HDR_OCTAGON_HA(p);
    octagonHole *newHole;

    // check if is really receive this hole's information
    bool isReceived = false;
    for (octagonHole *hole = octagonHole_list_; hole && !isReceived; hole = hole->next_) {
        isReceived = hole->hole_id_ == edh->id_;
    }

    if (!isReceived) {
        // ----------- add new circle hole
        newHole = (octagonHole *) malloc(sizeof(octagonHole));
        newHole->hole_id_ = edh->id_;
        newHole->pc_ = edh->pc_;
        newHole->delta_ = edh->delta_;
        newHole->d_ = edh->d_;
        newHole->node_list_ = NULL;
        newHole->next_ = octagonHole_list_;
        octagonHole_list_ = newHole;

        for (int i = 0; i < edh->vertex_num_; i++) {
            node *newNode = (node *) malloc(sizeof(node));
            newNode->x_ = edh->vertex[i].x_;
            newNode->y_ = edh->vertex[i].y_;
            newNode->next_ = newHole->node_list_;
            newHole->node_list_ = newNode;
        }

        // cycle the node_list
        node *temp = newHole->node_list_;
        while (temp->next_) temp = temp->next_;
        temp->next_ = newHole->node_list_;

        // ----------- broadcast hole's information. Check if (1) is satisfy
        alpha_ = 0;

        node *i = newHole->node_list_;
        ln_ = G::distance(this, i);
        do {
            for (node *j = i->next_; j != newHole->node_list_; j = j->next_) {
                double angle = G::angle(this, i, j);
                alpha_ = angle > alpha_ ? angle : alpha_;
            }

            double dist = G::distance(this, i);
            ln_ = dist > ln_ ? dist : ln_;

            i = i->next_;
        } while (i != newHole->node_list_);

        // check condition (1) to continues broadcast or not
        if ((alpha_ == M_PI) ||
            (newHole->pc_ / ln_ * (0.3 / cos(alpha_ / 2) + 1) + 1 / cos(alpha_ / 2)) > (fsin3pi8 + stretch_)) {
            broadcast_timer_.setParameter(p);
            broadcast_timer_.resched(randSend_.uniform(0.0, 0.5));
        }
        else {
            drop(p, "region_limited");
        }

        region_ = REGION_1;

        // ---------- dump
        dumpBroadcast();
    }
    else {
        drop(p, "broadcast_received");
    }
}

void OctagonAgent::forwardBroadcast(Packet *p) {
    hdr_cmn *cmh = HDR_CMN(p);
    cmh->direction() = hdr_cmn::DOWN;
    cmh->last_hop_ = my_id_;
    send(p, 0);
}

// --------------------- Routing --------------------------------- //
void OctagonAgent::dynamicRouting(Packet *p, OCTAGON_REGION region) {
    hdr_octagon_data *odh = HDR_OCTAGON_DATA(p);

    Point S = odh->vertex[1];
    Point D = odh->vertex[0];

    Point I;
    octagonHole *scaleHole;
    octagonHole *h = octagonHole_list_;

    // reset routing table. // Add destination to routing table
    odh->vertex_num_ = 1;

    // check if SD is intersection with hole => need to routing to avoid this hole
    int numIntersect = 0;
    node *n = h->node_list_;
    do {
        if (G::is_in_line(n, this, &D) && G::is_in_line(n->next_, this, &D)) break;
        if (G::is_intersect(n, n->next_, this, &D)) numIntersect++;
        n = n->next_;
    }
    while (n != h->node_list_);

    // Add sub destination to routing table
    if (numIntersect > 1) {
        double l = G::distance(&S, &D);

        // --------- scale hole
        // choose I random
        I.x_ = 0;
        I.y_ = 0;

        /* define 8 lines for new approximate hole
         *
         * 	|	    ____[2]_________
         * 	|	[02]	 			[21]______
         * 	|   /		 					  \
         * 	[0] ----------------------------- [1]
         * 	|   \____                       ___/
         * 	|.(I)	[30]____   ________[13]
         * 	|_______________[3]___________________
         */
        double fr = 0;
        n = h->node_list_;
        do {
            int ra = rand();
            I.x_ += n->x_ * ra;
            I.y_ += n->y_ * ra;
            fr += ra;

            n = n->next_;
        } while (n != h->node_list_);

        I.x_ = I.x_ / fr;
        I.y_ = I.y_ / fr;

        // calculate i
        if (scale_factor_ == -1) {
            if (region == REGION_1) {
                scale_factor_ = 1 + (l / h->pc_) * (fsin3pi8 / (1 + h->pc_ / (2 * (l - h->d_))) - 1);
            } else if (region == REGION_2) {
                scale_factor_ = (fsin3pi8 + stretch_ - 1 / cos(alpha_ / 2)) * l / h->pc_ - 0.3 / cos(alpha_ / 2);
            }
            if (scale_factor_ < 1) scale_factor_ = 1;
        }

        // scale hole by I and i
        scaleHole = new octagonHole();
        scaleHole->next_ = NULL;
        scaleHole->node_list_ = NULL;

        // add new node
        n = h->node_list_;
        do {
            node *newNode = new node();
            newNode->x_ = scale_factor_ * n->x_ + (1 - scale_factor_) * I.x_;
            newNode->y_ = scale_factor_ * n->y_ + (1 - scale_factor_) * I.y_;
            newNode->next_ = scaleHole->node_list_;
            scaleHole->node_list_ = newNode;

            n = n->next_;
        } while (n != h->node_list_);

        // circle node list
        n = scaleHole->node_list_;
        while (n->next_) n = n->next_;
        n->next_ = scaleHole->node_list_;

        // bypassing hole
        odh->vertex_num_ = 0;
        addrouting(&D, odh->vertex, odh->vertex_num_);
        bypassingHole(scaleHole, &D, odh->vertex, odh->vertex_num_);

        dumpDynamicRouting(p, scaleHole);

        // free
        n = scaleHole->node_list_;
        do {
            free(n);
            n = n->next_;
        } while (n != scaleHole->node_list_);

        free(scaleHole);
    }
}

void OctagonAgent::bypassingHole(octagonHole *h, Point *D, Point *routingTable, int &routingCount) {
    // create routing table for packet p
    node *S1 = NULL;    // min angle view of this node to hole
    node *S2 = NULL;    // max angle view of this node to hole
    node *D1 = NULL;    // min angle view of Dt node to hole
    node *D2 = NULL;    // max angle view of Dt node to hole

    // ------------------- S1 S2 - view angle of this node to hole
    double Smax = 0;
    double Dmax = 0;

    node *i = h->node_list_;
    do {
        for (node *j = i->next_; j != h->node_list_; j = j->next_) {
            // S1 S2
            double angle = G::angle(this, i, j);
            if (angle > Smax) {
                Smax = angle;
                S1 = i;
                S2 = j;
            }
            // D1 D2
            angle = G::angle(D, i, j);
            if (angle > Dmax) {
                Dmax = angle;
                D1 = i;
                D2 = j;
            }
        }

        i = i->next_;
    } while (i != h->node_list_);

    // if S1 and D1 are lie in different side of SD => switch D1 and D2
    Line SD = G::line(this, D);
    if (G::position(S1, SD) != G::position(D1, SD)) {
        node *temp = D1;
        D1 = D2;
        D2 = temp;
    }
    //free(SD);

    double min = 100000;
    // ------------------------------------------------- S S1 D1 D
    double dis = G::distance(this, S1);
    for (i = S1; i != D1; i = i->next_) {
        dis += G::distance(i, i->next_);
    }
    dis += G::distance(D1, D);
    if (dis < min) {
        min = dis;
        routingCount = 1;
        for (i = S1; i != D1->next_; i = i->next_) {
            addrouting(i, routingTable, routingCount);
        }
        // replace routing_table
        for (int index = 1; index <= (routingCount - 1) / 2; index++) {
            Point temp = routingTable[index];
            routingTable[index] = routingTable[routingCount - index];
            routingTable[routingCount - index] = temp;
        }
    }
    // ------------------------------------------------- S S2 D2 D
    dis = G::distance(this, S2);
    for (i = S2; i != D2; i = i->next_) {
        dis += G::distance(i, i->next_);
    }
    dis += G::distance(D2, D);
    if (dis < min) {
        min = dis;
        routingCount = 1;
        for (i = S2; i != D2->next_; i = i->next_) {
            addrouting(i, routingTable, routingCount);
        }
        // replace routing_table
        for (int index = 1; index <= (routingCount - 1) / 2; index++) {
            Point temp = routingTable[index];
            routingTable[index] = routingTable[routingCount - index];
            routingTable[routingCount - index] = temp;
        }
    }
    // ------------------------------------------------- D D1 S1 S
    dis = G::distance(D, D1);
    for (i = D1; i != S1; i = i->next_) {
        dis += G::distance(i, i->next_);
    }
    dis += G::distance(S1, this);
    if (dis < min) {
        min = dis;
        routingCount = 1;
        for (i = D1; i != S1->next_; i = i->next_) {
            addrouting(i, routingTable, routingCount);
        }
    }
    // ------------------------------------------------- D D2 S2 S
    dis = G::distance(D, D2);
    for (i = D2; i != S2; i = i->next_) {
        dis += G::distance(i, i->next_);
    }
    dis += G::distance(S2, this);
    if (dis < min) {
        min = dis;
        routingCount = 1;
        for (i = D2; i != S2->next_; i = i->next_) {
            addrouting(i, routingTable, routingCount);
        }
    }
}

void OctagonAgent::addrouting(Point *p, Point *routingTable, int &routingCount) {
    for (int i = 0; i < routingCount; i++) {
        if (routingTable[i].x_ == p->x_ && routingTable[i].y_ == p->y_)
            return;
    }

    routingTable[routingCount].x_ = p->x_;
    routingTable[routingCount].y_ = p->y_;
    routingCount++;
}

// --------------------- Send data -------------------------------- //

void OctagonAgent::sendData(Packet *p) {
    hdr_cmn *cmh = HDR_CMN(p);
    hdr_ip *iph = HDR_IP(p);
    hdr_octagon_data *edh = HDR_OCTAGON_DATA(p);

    cmh->size() += IP_HDR_LEN + edh->size();
    cmh->direction() = hdr_cmn::DOWN;

    edh->type_ = OCTAGON_DATA_GREEDY;
    edh->daddr_ = iph->daddr();
    edh->vertex_num_ = 1;
    edh->vertex[0] = *dest;
    edh->vertex[1] = *this;
    edh->gprs_type_ = GPSR_GPSR;

    iph->saddr() = my_id_;
    iph->daddr() = -1;
    iph->ttl_ = 4 * IP_DEF_TTL;
}

void OctagonAgent::recvData(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_ip *iph = HDR_IP(p);
    struct hdr_octagon_data *edh = HDR_OCTAGON_DATA(p);

    if (cmh->direction() == hdr_cmn::UP && edh->daddr_ == my_id_)    // up to destination
    {
        port_dmux_->recv(p, 0);
        return;
    }
    else {
        if (region_ == REGION_1 && iph->saddr() != my_id_ && edh->type_ == OCTAGON_DATA_GREEDY) {
            // source is in region 2, come to sub source => convert mode
            dynamicRouting(p, REGION_2);
            edh->type_ = OCTAGON_DATA_ROUTING;
        } else if (region_ == REGION_1 && iph->saddr() == my_id_ && edh->type_ == OCTAGON_DATA_GREEDY) {
            // source is in region 1 => convert mode
            dynamicRouting(p, REGION_1);
            edh->type_ = OCTAGON_DATA_ROUTING;
        }
    }

    // -------- forward by greedy
    node *nexthop = NULL;
    while (nexthop == NULL && edh->vertex_num_ > 0) {
        if (edh->vertex_num_ == 1)
            nexthop = recvGPSR(p, edh->vertex[edh->vertex_num_ - 1]);
        else
            nexthop = getNeighborByGreedy(edh->vertex[edh->vertex_num_ - 1]);
        if (nexthop == NULL) {
            edh->vertex_num_--;
        }
    }

    if (nexthop == NULL)    // no neighbor close
    {
        drop(p, DROP_RTR_NO_ROUTE);
        return;
    }
    else {
        cmh->direction() = hdr_cmn::DOWN;
        cmh->addr_type() = NS_AF_INET;
        cmh->last_hop_ = my_id_;
        cmh->next_hop_ = nexthop->id_;
        send(p, 0);
    }
}

node *OctagonAgent::recvGPSR(Packet *p, Point destionation) {
    struct hdr_octagon_data *egh = HDR_OCTAGON_DATA(p);

    node *nb = NULL;

    switch (egh->gprs_type_) {
        case GPSR_GPSR:
            nb = this->getNeighborByGreedy(destionation, *this);

            if (nb == NULL) {
                nb = getNeighborByPerimeter(destionation);

                if (nb == NULL) {
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
                    return NULL;
                }
            }
            break;

        default:
            return NULL;
    }

    egh->prev_ = *this;

    return nb;
}

// --------------------- Dump ------------------------------------- //

void OctagonAgent::dumpApproximateHole() {
    FILE *fp = fopen("ApproximateHole.tr", "a+");
    for (struct octagonHole *h = octagonHole_list_; h; h = h->next_) {
        node *n = h->node_list_;
        do {
            fprintf(fp, "%f	%f\n", n->x_, n->y_);
            n = n->next_;
        }
        while (n != h->node_list_);
        fprintf(fp, "%f	%f\n", n->x_, n->y_);
        fprintf(fp, "\n");
    }
    fclose(fp);
}

void OctagonAgent::dumpDynamicRouting(Packet *p, octagonHole *hole) {
    hdr_cmn *cmh = HDR_CMN(p);

    FILE *fp = fopen("DynamicScaleHole.tr", "a+");

    node *n = hole->node_list_;
    do {
        fprintf(fp, "%d	%f	%f\n", cmh->uid_, n->x_, n->y_);
        n = n->next_;
    }
    while (n != hole->node_list_);

    fprintf(fp, "%d	%f	%f\n", cmh->uid_, n->x_, n->y_);
    fprintf(fp, "\n");

    fclose(fp);
}

void OctagonAgent::dumpBroadcast() {
    FILE *fp = fopen("BroadcastRegion.tr", "a+");

    fprintf(fp, "%f	%f\n", this->x_, this->y_);

    fclose(fp);
}
