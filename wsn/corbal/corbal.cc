#include <packet.h>
#include "corbal_packet.h"
#include "corbal.h"
#include "corbal_packet_data.h"

int hdr_corbal::offset_;

static class CorbalHeaderClass : public PacketHeaderClass
{
public:
    CorbalHeaderClass() : PacketHeaderClass("PacketHeader/CORBAL", sizeof(hdr_corbal))
    {
        bind_offset(&hdr_corbal::offset_);
    }
}class_corbalhdr;

static class CorbalAgentClass : public TclClass {
public:
    CorbalAgentClass() : TclClass("Agent/CORBAL") {}
    TclObject* create(int, const char*const*) {
        return (new CorbalAgent());
    }
}class_corbal;

void
CorbalTimer::expire(Event *e) {
    (a_->*firing_)();
}

/**
 * Agent Implementatoion
 */
CorbalAgent::CorbalAgent() : GPSRAgent(),
                                   findStuck_timer_(this, &CorbalAgent::findStuckAngle),
                                   boundhole_timer_(this, &CorbalAgent::sendBoundHole)
{
    stuck_angle_ = NULL;
    range_ = 40;
    limit_min_hop_ = 10;
    limit_max_hop_ = 80;
    n_ = 8;
    core_polygon_set = NULL;
    hole_ = NULL;

    bind("range_", &range_);
    bind("limit_boundhole_hop_", &limit_max_hop_);
    bind("min_boundhole_hop_", &limit_min_hop_);
    bind("n_", &n_);

    theta_n = 2 * M_PI / ((n_ + 1) * floor(12 / (n_ + 1)));
    kn = (int)floor((n_ - 2) * M_PI / (n_ * theta_n));
}

int
CorbalAgent::command(int argc, const char*const* argv)
{
    if (argc == 2)
    {
        if (strcasecmp(argv[1], "start") == 0)
        {
            startUp();
        }
        if (strcasecmp(argv[1], "boundhole") == 0)
        {
            boundhole_timer_.resched(randSend_.uniform(0.0, 5));
            return TCL_OK;
        }
    }

    return GPSRAgent::command(argc,argv);
}

void
CorbalAgent::recv(Packet *p, Handler *h)
{
    hdr_cmn 		*cmh = HDR_CMN(p);
    hdr_ip  		*iph = HDR_IP(p);
    hdr_corbal	    *bhh = HDR_CORBAL(p);

    switch (cmh->ptype())
    {
        case PT_HELLO:
            GPSRAgent::recv(p, h);
            break;

        case PT_CORBAL:
            if (bhh->type_ == CORBAL_BOUNDHOLE)
            {
                recvBoundHole(p);
            }
            else if(bhh->type_ == CORBAL_HBA) // bhh->type_ == CORBAL_HBA
            {
                recvHBA(p);
            }
            break;

        case PT_CBR:
            if (iph->saddr() == my_id_)				// a packet generated by myself
            {
                if (cmh->num_forwards() == 0)		// a new packet
                {
                    sendData(p);
                }
                else	//(cmh->num_forwards() > 0)	// routing loop -> drop
                {
                    drop(p, DROP_RTR_ROUTE_LOOP);
                    return;
                }
            }

            if (iph->ttl_-- <= 0)
            {
                drop(p, DROP_RTR_TTL);
                return;
            }
            recvData(p);
            break;

        default:
            drop(p, " UnknowType");
            break;
    }
}

void
CorbalAgent::startUp()
{
    findStuck_timer_.resched(20);

    // clear trace file
    FILE *fp;
    fp = fopen("Neighbors.tr", "w");	fclose(fp);
    fp = fopen("BoundHole.tr", "w");	fclose(fp);
    fp = fopen("CorePolygon.tr", "w");  fclose(fp);
    fp = fopen("debug.tr", "w");        fclose(fp);
}

/*
 * Boundhole phase
 */
void
CorbalAgent::findStuckAngle()
{
    if (neighbor_list_ == NULL || neighbor_list_->next_ == NULL)
    {
        stuck_angle_ = NULL;
        return;
    }

    node *nb1 = neighbor_list_; //u
    node *nb2 = neighbor_list_->next_; //v

    while (nb2)
    {
        Circle circle = G::circumcenter(this, nb1, nb2);
        Angle a = G::angle(this, nb1, this, &circle); // upO
        Angle b = G::angle(this, nb1, this, nb2); // upv
        Angle c = G::angle(this, &circle, this, nb2); //Opv

        // if O is outside range of node, nb1 and nb2 create a stuck angle with node
        if (b >= M_PI || (fabs(a) + fabs(c) == fabs(b) && G::distance(this, circle) > range_))
        {
            stuckangle* new_angle = new stuckangle();
            new_angle->a_ = nb1;
            new_angle->b_ = nb2;
            new_angle->next_ = stuck_angle_;
            stuck_angle_ = new_angle;
        }

        nb1 = nb1->next_;
        nb2 = nb1->next_;
    }

    nb2 = neighbor_list_;
    Circle circle = G::circumcenter(this, nb1, nb2);
    Angle a = G::angle(this, nb1, this, &circle);
    Angle b = G::angle(this, nb1, this, nb2);
    Angle c = G::angle(this, &circle, this, nb2);

    // if O is outside range of node, nb1 and nb2 create a stuck angle with node
    if (b >= M_PI || (fabs(a) + fabs(c) == fabs(b) && G::distance(this, circle) > range_))
    {
        stuckangle* new_angle = new stuckangle();
        new_angle->a_ = nb1;
        new_angle->b_ = nb2;
        new_angle->next_ = stuck_angle_;
        stuck_angle_ = new_angle;
    }
}

void CorbalAgent::sendBoundHole()
{
    Packet			*p;
    hdr_cmn			*cmh;
    hdr_ip			*iph;
    hdr_corbal  	*bhh;

    for (stuckangle * sa = stuck_angle_; sa; sa = sa->next_)
    {
        p = allocpkt();

        CorbalPacketData *bhpkt_data = new CorbalPacketData();
        bhpkt_data->add(sa->b_->id_, sa->b_->x_, sa->b_->y_);
        bhpkt_data->add(my_id_, this->x_, this->y_);
        p->setdata(bhpkt_data);

        cmh = HDR_CMN(p);
        iph = HDR_IP(p);
        bhh = HDR_CORBAL(p);

        cmh->ptype() 	 = PT_CORBAL;
        cmh->direction() = hdr_cmn::DOWN;
        cmh->size()		 += IP_HDR_LEN + bhh->size() + bhpkt_data->data_len_;
        cmh->next_hop_	 = sa->a_->id_;
        cmh->last_hop_ 	 = my_id_;
        cmh->addr_type_  = NS_AF_INET;

        iph->saddr() = my_id_;
        iph->daddr() = sa->a_->id_;
        iph->sport() = RT_PORT;
        iph->dport() = RT_PORT;
        iph->ttl_ 	 = limit_max_hop_;			// more than ttl_ hop => boundary => remove

        bhh->prev_ = *this;
        bhh->type_ = CORBAL_BOUNDHOLE;

        send(p, 0);
    }
}

void CorbalAgent::recvBoundHole(Packet *p)
{
    struct hdr_ip *iph = HDR_IP(p);
    struct hdr_cmn 		 *cmh = HDR_CMN(p);
    struct hdr_corbal *bhh = HDR_CORBAL(p);

    CorbalPacketData *data = (CorbalPacketData*)p->userdata();

    // if the boundhole packet has came back to the initial node
    if (iph->saddr() == my_id_)
    {
        if (iph->ttl_ > (limit_max_hop_ - limit_min_hop_))
        {
            drop(p, " SmallHole");	// drop hole that have less than limit_min_hop_ hop
        }
        else
        {
            hole_ = createPolygonHole(p);
            data->dump();
            // starting sending HBA to hole boundary
            sendHBA(p);
        }
        return;
    }

    if (iph->ttl_-- <= 0)
    {
        drop(p, DROP_RTR_TTL);
        return;
    }

//	int lastcount = data->data_len_;

    node n = data->get_data(1);
    if (n.id_ != iph->saddr())
    {
        // if this is second hop => remove "b"
        data->rmv_data(1);
    }
    else
    {
        // get the entry at index n-1
        n = data->get_data(data->size() - 1);
    }

    node* nb = getNeighborByBoundHole(&bhh->prev_, &n);

    // no neighbor to forward, drop message
    // it means the network is not interconnected
    if (nb == NULL)
    {
        drop(p, DROP_RTR_NO_ROUTE);
        return;
    }

    // drop message for loop way
    node temp, next;

    for (int i = 1; i < data->size(); i++)
    {
        temp = data->get_data(i);
        next = data->get_data(i + 1);

        if (G::is_intersect2(this, nb, temp, next))
        {
            if (G::distance(temp, nb) < range_)
            {
                while (data->size() >= (i + 1))
                {
                    data->rmv_data(i + 1);
                }

                data->add(nb->id_, nb->x_, nb->y_);
                nb = getNeighborByBoundHole(nb, &temp);
                if(nb == NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return;
                }
                continue;
            }
            else
            {
                nb = getNeighbor(next.id_);
                if(nb==NULL) {
                    drop(p, DROP_RTR_NO_ROUTE);
                    return;
                }
                continue;
            }
        }
    }

    // if neighbor already send boundhole message to that node
    if (iph->saddr() > my_id_)
    {
        for (stuckangle *sa = stuck_angle_; sa; sa = sa->next_)
        {
            if (sa->a_->id_ == nb->id_)
            {
                drop(p, "BOUNDHOLE_REPEAT");
                return;
            }
        }
    }

    data->add(my_id_, this->x_, this->y_);

    cmh->direction() = hdr_cmn::DOWN;
    cmh->next_hop_ = nb->id_;
    cmh->last_hop_ = my_id_;
//	cmh->size_ += (data->data_len_ - lastcount) * data->element_size_;

    iph->daddr() = nb->id_;

    bhh->prev_ = *this;

    send(p, 0);
}

node* CorbalAgent::getNeighborByBoundHole(Point * p, Point * prev)
{
    Angle max_angle = -1;
    node* nb = NULL;

    for (node * temp = neighbor_list_; temp; temp = temp->next_)
    {
        Angle a = G::angle(this, p, this, temp);
        if (a > max_angle && (!G::is_intersect(this, temp, p, prev) ||
                              (temp->x_ == p->x_ && temp->y_ == p->y_) ||
                              (this->x_ == prev->x_ && this->y_ == prev->y_)))
        {
            max_angle = a;
            nb = temp;
        }
    }

    return nb;
}

/*
 * HBA phase
 */
void CorbalAgent::sendHBA(Packet *p) 
{
    CorbalPacketData* 	data = (CorbalPacketData*)p->userdata();
    hdr_cmn*	 	cmh	= HDR_CMN(p);
    hdr_ip*			iph = HDR_IP(p);
    hdr_corbal*     bhh = HDR_CORBAL(p);

    // update data payload - alloc memory for set of B(i) nodes
    data->add(my_id_, x_, y_); // add back H0 to end of array
    data->addHBA(n_, kn);
    isNodeStayOnBoundaryOfCorePolygon(p);

    node n = data->get_data(2);

    cmh->direction() 	= hdr_cmn::DOWN;
    cmh->next_hop_ 		= n.id_;
    cmh->last_hop_ 		= my_id_;
    cmh->addr_type_ 	= NS_AF_INET;
    cmh->ptype() 		= PT_CORBAL;
    cmh->num_forwards() = 0;
    cmh->size()		 += IP_HDR_LEN + bhh->size() + data->data_len_;

    iph->saddr() = my_id_;
    iph->daddr() = n.id_;
    iph->sport() = RT_PORT;
    iph->dport() = RT_PORT;
    iph->ttl_ 	 = IP_DEF_TTL;

    bhh->type_ = CORBAL_HBA;

    send(p, 0);
}

void CorbalAgent::recvHBA(Packet *p) 
{
    struct hdr_ip 		*iph = HDR_IP(p);
    CorbalPacketData    *data = (CorbalPacketData*)p->userdata();

    hole_ = createPolygonHole(p);
    int i = 2;
    while (data->get_data(i).id_ != my_id_) i++;

    if (i < data->size() - (n_ + 1) * kn)
    {
        isNodeStayOnBoundaryOfCorePolygon(p);
        nsaddr_t next_id = data->get_data(i+1).id_;

        hdr_cmn *cmh = HDR_CMN(p);
        cmh->direction_ = hdr_cmn::DOWN;
        cmh->last_hop_  = my_id_;
        cmh->next_hop_  = next_id;

        iph->daddr() = next_id;

        send(p, 0);
    }
    else { // back to H0
        contructCorePolygonSet(p);
        drop(p, "BOUNDHOLE_HBA");
    }
}

void CorbalAgent::contructCorePolygonSet(Packet *p)
{
    CorbalPacketData* data = (CorbalPacketData*)p->userdata();
    int data_size = data->size() - (n_ + 1) * kn;
    int off = 0;

    for(int i = 1; i<= kn; i++) {
        corePolygon *new_core = new corePolygon();
        new_core->id_ = i;

        for(int j = 1; j <= n_; j++) {
            int j_1 = j == n_ ? 1 : j + 1;

            off = data_size + (n_ + 1) * (i - 1) + j;
            node b_j    = data->get_Bi_data(off);
            off = data_size + (n_ + 1) * (i - 1) + j_1;
            node b_j_1  = data->get_Bi_data(off);

            Angle b_j_angle     = i * theta_n + j * 2 * M_PI / n_;
            Angle b_j_1_angle   = i * theta_n + j_1 * 2 * M_PI /n_;

            Line l_i_j   = G::line(b_j, b_j_angle);
            Line l_i_j_1 = G::line(b_j_1, b_j_1_angle);

            Point intersection;
            G::intersection(l_i_j, l_i_j_1, intersection);
            addCorePolygonNode(intersection, new_core);
        }

        new_core->next_ = core_polygon_set;
        core_polygon_set = new_core;
    }

    dumpCorePolygon();
}

void CorbalAgent::addCorePolygonNode(Point newPoint, corePolygon *corePolygon)
{
    for (node* i = corePolygon->node_; i; i = i->next_)
    {
        if (*i == newPoint)	return;
    }
    node * newNode	= new node();
    newNode->x_ 	= newPoint.x_;
    newNode->y_ 	= newPoint.y_;
    newNode->next_ 	= corePolygon->node_;
    corePolygon->node_= newNode;
}

// check if this node is on boundary of a core polygon
// if true then update the packet data payload for each (i,j) immediately
void CorbalAgent::isNodeStayOnBoundaryOfCorePolygon(Packet *p)
{
    CorbalPacketData *data = (CorbalPacketData*)p->userdata();
    int data_size = data->size() - (n_ + 1) * kn;
    int off = 0;

    int i;
    for(i = 1; i <= kn; i++) {
        bool first_time = false;
        // get next index of this B(i)

        off = data_size + (n_ + 1) * (i-1);
        int next_index = data->get_next_index_of_Bi(off);

        if(next_index == 1) {
            next_index = n_ + 1;
        }
        else if(next_index == 0) {
            next_index = n_ + 1;
            first_time = true;
        }

        while(true)
        {
            next_index--;
            bool flag = false;
            double angle = i * theta_n + next_index * 2 * M_PI / n_;
            // draw line goes through this node and make with x-axis angle: mx + n = y
            Line l_n = G::line(this, angle);

            node tmp1, tmp2;
            int index;

            tmp1 = data->get_data(1).id_ == my_id_ ? data->get_data(2) : data->get_data(1);
            for(index = 1; index < data_size; index++)
            {
                tmp2 = data->get_data(index);
                if(tmp2.id_ == my_id_) continue;
                if (G::position(&tmp1, &tmp2, &l_n) < 0) {
                    flag = true;
                    break;
                }
            }

            if(next_index <= 0) break;
            if(flag && !first_time) break;
            if(flag) continue;

            first_time = false;
            // add N to set B(i, j)
            off = (i - 1) * (n_ + 1) + next_index + data_size;
            data->addBiNode(off, my_id_, x_, y_);
            off = data_size + (i - 1) * (n_ + 1);
            data->update_next_index_of_Bi(off, next_index);
            dump(angle, i, next_index, l_n);
            printf("i = %d j = %d nodeid = %d\n", i, next_index, my_id_);
        }
    }
}


polygonHole* CorbalAgent::createPolygonHole(Packet *p) {
    CorbalPacketData* data = (CorbalPacketData*)p->userdata();

    // create hole item
    polygonHole * hole_item = new polygonHole();
    hole_item->hole_id_ 	= my_id_;
    hole_item->node_list_ 	= NULL;

    // add node info to hole item
    struct node* item;

    for (int i = 1; i <= data->size(); i++)
    {
        node n = data->get_data(i);

        item = new node();
        item->x_	= n.x_;
        item->y_	= n.y_;
        item->next_ = hole_item->node_list_;
        hole_item->node_list_ = item;
    }

    return hole_item;
}

/*
 * HCI broadcast phase
 */
void CorbalAgent::broadcastHCI(Packet *packet) {

}

void CorbalAgent::recvHCI(Packet *packet) {

}

/**
 * Routing phase
 */
void CorbalAgent::sendData(Packet *packet) {

}

void CorbalAgent::recvData(Packet *packet) {

}

/**
 * Dump methods
 */
void CorbalAgent::dumpCorePolygon() {
    FILE *fp = fopen("CorePolygon.tr", "a+");

    for(corePolygon *tmp = core_polygon_set; tmp != NULL; tmp = tmp->next_) {
        fprintf(fp, "%d\n", tmp->id_);
        for(node *node = tmp->node_; node!= NULL; node = node->next_) {
            fprintf(fp, "%f\t%f\n", node->x_, node->y_);
        }
    }
    fclose(fp);
}

void CorbalAgent::dump(Angle a, int i, int j, Line ln)
{
    FILE *fp = fopen("debug.tr", "a+");
    fprintf(fp, "%d\t%f\t%f\t%f\t%d\t%d\t%f\t%f\t%f\t\n", my_id_, x_, y_, a, i, j, ln.a_, ln.b_, ln.c_);
    fclose(fp);
}
