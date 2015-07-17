/*
 * grid.cc
 *
 *  Last edited on Nov 14, 2013
 *  by Trong Nguyen
 */

#include "config.h"
#include "gridoffline.h"
#include "gridoffline_packet_data.h"
#include "wsn/geomathhelper/geo_math_helper.h"

#include "../include/tcl.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int hdr_grid::offset_;

static class GridOfflineHeaderClass : public PacketHeaderClass {
public:
    GridOfflineHeaderClass() : PacketHeaderClass("PacketHeader/GRID", sizeof(hdr_grid)) {
        bind_offset(&hdr_grid::offset_);
    }
} class_gridhdr;

static class GridOfflineAgentClass : public TclClass {
public:
    GridOfflineAgentClass() : TclClass("Agent/GRIDOFFLINE") {
    }

    TclObject *create(int, const char *const *) {
        return (new GridOfflineAgent());
    }
} class_grid;

void
GridOfflineTimer::expire(Event *e) {
    (a_->*firing_)();
}

// ------------------------ Agent ------------------------ //

GridOfflineAgent::GridOfflineAgent() : GPSRAgent(),
                                       findStuck_timer_(this, &GridOfflineAgent::findStuckAngle),
                                       grid_timer_(this, &GridOfflineAgent::sendBoundHole) {
    stuck_angle_ = NULL;
    hole_list_ = NULL;
    bind("range_", &range_);
    bind("limit_", &limit_);
    bind("r_", &r_);
    bind("limit_boundhole_hop_", &limit_boundhole_hop_);
}

char const * GridOfflineAgent::getAgentName() {
    return "GridOffline";
}

int
GridOfflineAgent::command(int argc, const char *const *argv) {
    if (argc == 2) {
        if (strcasecmp(argv[1], "start") == 0) {
            startUp();
        }
        if (strcasecmp(argv[1], "boundhole") == 0) {
            grid_timer_.resched(randSend_.uniform(0.0, 5));
            return TCL_OK;
        }
        if (strcasecmp(argv[1], "routing") == 0) {
            return TCL_OK;
        }
        if (strcasecmp(argv[1], "bcenergy") == 0) {
            dumpEnergy();
            return TCL_OK;
        }
    }

    return GPSRAgent::command(argc, argv);
}

// handle the receive packet just of type PT_GRID
void
GridOfflineAgent::recv(Packet *p, Handler *h) {
    hdr_cmn *cmh = HDR_CMN(p);

    switch (cmh->ptype()) {
        case PT_HELLO:
            GPSRAgent::recv(p, h);
            break;

        case PT_GRID:
            recvBoundHole(p);
            break;

//		case PT_CBR:
//			if (iph->saddr() == my_id_)				// a packet generated by myself
//			{
//				if (cmh->num_forwards() == 0)		// a new packet
//				{
//					sendData(p);
//				}
//				else	//(cmh->num_forwards() > 0)	// routing loop -> drop
//				{
//					drop(p, DROP_RTR_ROUTE_LOOP);
//					return;
//				}
//			}
//
//			if (iph->ttl_-- <= 0)
//			{
//				drop(p, DROP_RTR_TTL);
//				return;
//			}
//			recvData(p);
//			break;

        default:
            drop(p, " UnknowType");
            break;
    }
}

void
GridOfflineAgent::startUp() {
    if (hello_period_ > 0)
        findStuck_timer_.resched(hello_period_ * 1.5);
    else
        findStuck_timer_.resched(20);

    // clear trace file
    initTraceFile();
}

// ------------------------ Bound hole ------------------------ //

void
GridOfflineAgent::findStuckAngle() {
    if (neighbor_list_ == NULL || neighbor_list_->next_ == NULL) {
        stuck_angle_ = NULL;
        return;
    }

    node *nb1 = neighbor_list_;
    node *nb2 = neighbor_list_->next_;

    while (nb2) {
        Circle circle = G::circumcenter(this, nb1, nb2);
        Angle a = G::angle(this, nb1, this, &circle);
        Angle b = G::angle(this, nb1, this, nb2);
        Angle c = G::angle(this, &circle, this, nb2);

        // if O is outside range of node, nb1 and nb2 create a stuck angle with node
        if (b >= M_PI || (fabs(a) + fabs(c) == fabs(b) && G::distance(this, circle) > range_)) {
            stuckangle *new_angle = new stuckangle();
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
    if (b >= M_PI || (fabs(a) + fabs(c) == fabs(b) && G::distance(this, circle) > range_)) {
        stuckangle *new_angle = new stuckangle();
        new_angle->a_ = nb1;
        new_angle->b_ = nb2;
        new_angle->next_ = stuck_angle_;
        stuck_angle_ = new_angle;
    }
}

void
GridOfflineAgent::sendBoundHole() {
    Packet *p;
    hdr_cmn *cmh;
    hdr_ip *iph;
    hdr_grid *bhh;

    for (stuckangle *sa = stuck_angle_; sa; sa = sa->next_) {
        p = allocpkt();

        p->setdata(new GridOfflinePacketData());

        cmh = HDR_CMN(p);
        iph = HDR_IP(p);
        bhh = HDR_GRID(p);

        cmh->ptype() = PT_GRID;
        cmh->direction() = hdr_cmn::DOWN;
        cmh->size() += IP_HDR_LEN + bhh->size();
        cmh->next_hop_ = sa->a_->id_;
        cmh->last_hop_ = my_id_;
        cmh->addr_type_ = NS_AF_INET;

        iph->saddr() = my_id_;
        iph->daddr() = sa->a_->id_;
        iph->sport() = RT_PORT;
        iph->dport() = RT_PORT;
        iph->ttl_ = limit_boundhole_hop_;            // more than ttl_ hop => boundary => remove

        bhh->prev_ = *this;
        bhh->last_ = *(sa->b_);
        bhh->i_ = *this;

        send(p, 0);

//        printf("%d\t- Send %s\n", my_id_, getAgentName());
    }
}

void
GridOfflineAgent::recvBoundHole(Packet *p) {
    // add data to packet
    addData(p);

    struct hdr_ip *iph = HDR_IP(p);
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_grid *bhh = HDR_GRID(p);

    // if the grid packet has came back to the initial node
    if (iph->saddr() == my_id_) {
        if (iph->ttl_ > (limit_boundhole_hop_ - 15)) {
            drop(p, " SmallHole");    // drop hole that have less than 15 hop
        }
        else {
            createPolygonHole(p);

            dumpBoundhole();
            dumpTime();
            dumpEnergy();
            dumpArea();

            drop(p, " GRID");
        }
        return;
    }

    if (iph->ttl_-- <= 0) {
        drop(p, DROP_RTR_TTL);
        return;
    }

    node *nb = getNeighborByBoundhole(&bhh->prev_, &bhh->last_);

    // no neighbor to forward, drop message. it means the network is not interconnected
    if (nb == NULL) {
        drop(p, DROP_RTR_NO_ROUTE);
        return;
    }

    // if neighbor already send grid message to that node
    if (iph->saddr() > my_id_) {
        for (stuckangle *sa = stuck_angle_; sa; sa = sa->next_) {
            if (sa->a_->id_ == nb->id_) {
                drop(p, " REPEAT");
                return;
            }
        }
    }

    cmh->direction() = hdr_cmn::DOWN;
    cmh->next_hop_ = nb->id_;
    cmh->last_hop_ = my_id_;

    iph->daddr() = nb->id_;

    bhh->last_ = bhh->prev_;
    bhh->prev_ = *this;

    send(p, 0);
}

node *
GridOfflineAgent::getNeighborByBoundhole(Point *p, Point *prev) {
    Angle max_angle = -1;
    node *nb = NULL;

    for (node *temp = neighbor_list_; temp; temp = temp->next_) {
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

// ------------------------ add data ------------------------ //

void
GridOfflineAgent::addData(Packet *p) {
    struct hdr_cmn *cmh = HDR_CMN(p);
    struct hdr_grid *bhh = HDR_GRID(p);
    GridOfflinePacketData *data = (GridOfflinePacketData *) p->userdata();

    // Add data to packet
    if (cmh->num_forwards_ == 1) {
        if (fmod(bhh->i_.x_, r_) == 0)    // i lies in vertical line
        {
            if (this->x_ > bhh->i_.x_) bhh->i_.x_ += r_ / 2;
            else if (this->x_ < bhh->i_.x_) bhh->i_.x_ -= r_ / 2;
            else // (this->x_ == bhh->i_.x_)
            {
                if (this->y_ > bhh->i_.y_) bhh->i_.x_ += r_ / 2;
                else bhh->i_.x_ -= r_ / 2;
            }
        }
        if (fmod(bhh->i_.y_, r_) == 0)    // i lies in h line
        {
            if (this->y_ > bhh->i_.y_) bhh->i_.y_ += r_ / 2;
            else if (this->y_ < bhh->i_.y_) bhh->i_.y_ -= r_ / 2;
            else // (this->y_ == bhh->i_.y_)
            {
                if (this->x_ > bhh->i_.x_) bhh->i_.y_ -= r_ / 2;
                else bhh->i_.y_ += r_ / 2;
            }
        }

        bhh->i_.x_ = ((int) (bhh->i_.x_ / r_) + 0.5) * r_;
        bhh->i_.y_ = ((int) (bhh->i_.y_ / r_) + 0.5) * r_;
    }

    Point i[4];
    Line l = G::line(bhh->prev_, this);

    while ((fabs(this->x_ - bhh->i_.x_) > r_ / 2) || (fabs(this->y_ - bhh->i_.y_) > r_ / 2)) {
        i[Up].x_ = bhh->i_.x_;
        i[Up].y_ = bhh->i_.y_ + r_;
        i[Left].x_ = bhh->i_.x_ - r_;
        i[Left].y_ = bhh->i_.y_;
        i[Down].x_ = bhh->i_.x_;
        i[Down].y_ = bhh->i_.y_ - r_;
        i[Right].x_ = bhh->i_.x_ + r_;
        i[Right].y_ = bhh->i_.y_;

        int m = this->x_ > bhh->prev_.x_ ? Right : Left;
        int n = this->y_ > bhh->prev_.y_ ? Up : Down;

        if (G::distance(i[m], l) > G::distance(i[n], l)) m = n;
        data->addData(m);
        bhh->i_ = i[m];
        cmh->size() = 68 + ceil((double) data->size() / 4);
    }
}

// ------------------------ Create PolygonHole ------------------------ //

void
GridOfflineAgent::createPolygonHole(Packet *p) {
    struct hdr_ip*		iph = HDR_IP(p);

    // check if is really receive this hole's information
    for (polygonHole* h = hole_list_; h; h = h->next_)
    {
        if (h->hole_id_ == iph->saddr())	// already received
        {
            drop(p, "Received");
            return;
        }
    }

    // add new Hole
    polygonHole *newHole = new polygonHole();
    newHole->hole_id_ = iph->saddr(); // set hole id
    newHole->node_list_ = NULL;
    newHole->next_ = hole_list_;
    hole_list_ = newHole;

    // get a, x0, y0
    GridOfflinePacketData *data = (GridOfflinePacketData *) p->userdata();
    data->dump();
    int minx = 0, maxx = 0, miny = 0, maxy = 0, x = 0, y = 0;

    for (int i = 1; i <= data->size(); i++) {
        switch (data->getData(i)) {
            case Up:
                y++;
                if (maxy < y) maxy = y;
                break;
            case Left:
                x--;
                if (minx > x) minx = x;
                break;
            case Down:
                y--;
                if (miny > y) miny = y;
                break;
            case Right:
                x++;
                if (maxx < x) maxx = x;
                break;
        }
    }

    int nx = maxx - minx + 1;
    int ny = maxy - miny + 1;
    bool **a = (bool **) malloc((nx) * sizeof(bool *));
    for (int i = 0; i < nx; i++)
        a[i] = (bool *) malloc((ny) * sizeof(bool));

    for (int i = 0; i < nx; i++)
        for (int j = 0; j < ny; j++) {
            a[i][j] = 0;
        }

    x = -minx;
    y = -miny;
    a[x][y] = 1;
    for (int i = 1; i <= data->size(); i++) {
        switch (data->getData(i)) {
            case Up:
                y++;
                break;
            case Left:
                x--;
                break;
            case Down:
                y--;
                break;
            case Right:
                x++;
                break;
        }
        a[x][y] = 1;
    }

    FILE *fp = fopen("a.tr", "w");
    for (int j = ny - 1; j >= 0; j--) {
        for (int i = 0; i < nx; i++) {
            fprintf(fp, "%d ", a[i][j]);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);

    // a - matrix that march the sub region bound the hole, with nx * ny cell.
    // cell i,j is painted when a[i][j] = 1
    // current node is located in the cell (x0, y0) = (-minx, -miny)

    // create polygon hole

    y = 0;
    x = nx - 1;
    while (a[x][y] == 0) x--;    // find the leftist cell that painted in the lowest row
    node *sNode = new node();
    sNode->x_ = (floor(this->x_ / r_) + x + minx + 1) * r_;
    sNode->y_ = (floor(this->y_ / r_) + miny) * r_;
    sNode->next_ = newHole->node_list_;
    newHole->node_list_ = sNode;

    while (x >= 0 && a[x][0] == 1) x--; // find the end cell of serial painted cell from left to right in the lowest row
    x++;
    Point n, u;
    n.x_ = (floor(this->x_ / r_) + x + minx) * r_;
    n.y_ = (floor(this->y_ / r_) + miny) * r_;

    while (n.x_ != sNode->x_ || n.y_ != sNode->y_) {
        u = *(newHole->node_list_);

        node *newNode = new node();
        newNode->x_ = n.x_;
        newNode->y_ = n.y_;
        newNode->next_ = newHole->node_list_;
        newHole->node_list_ = newNode;

        if (u.y_ == n.y_) {
            if (u.x_ < n.x_)        // >
            {
                if (y + 1 < ny && x + 1 < nx && a[x + 1][y + 1]) {
                    x += 1;
                    y += 1;
                    n.y_ += r_;
                }
                else if (x + 1 < nx && a[x + 1][y]) {
                    x += 1;
                    n.x_ += r_;
                    newHole->node_list_ = newNode->next_;
                    delete newNode;
                }
                else {
                    n.y_ -= r_;
                }
            }
            else // u->x_ > v->x_			// <
            {
                if (y - 1 >= 0 && x - 1 >= 0 && a[x - 1][y - 1]) {
                    x -= 1;
                    y -= 1;
                    n.y_ -= r_;
                }
                else if (x - 1 >= 0 && a[x - 1][y]) {
                    x -= 1;
                    n.x_ -= r_;
                    newHole->node_list_ = newNode->next_;
                    delete newNode;
                }
                else {
                    n.y_ += r_;
                }
            }
        }
        else    // u->x_ == v->x_
        {
            if (u.y_ < n.y_)        // ^
            {
                if (y + 1 < ny && x - 1 >= 0 && a[x - 1][y + 1]) {
                    x -= 1;
                    y += 1;
                    n.x_ -= r_;
                }
                else if (y + 1 < ny && a[x][y + 1]) {
                    y += 1;
                    n.y_ += r_;
                    newHole->node_list_ = newNode->next_;
                    delete newNode;
                }
                else {
                    n.x_ += r_;
                }
            }
            else // u.x > n.x		// v
            {
                if (y - 1 >= 0 && x + 1 < nx && a[x + 1][y - 1]) {
                    x += 1;
                    y -= 1;
                    n.x_ += r_;
                }
                else if (y - 1 >= 0 && a[x][y - 1]) {
                    y -= 1;
                    n.y_ -= r_;
                    newHole->node_list_ = newNode->next_;
                    delete newNode;
                }
                else {
                    n.x_ -= r_;
                }
            }
        }
    }

    // reduce polygon hole
    reducePolygonHole(newHole);
}

void
GridOfflineAgent::reducePolygonHole(polygonHole *h) {
    if (limit_ >= 4) {
        int count = 0;
        for (node *n = h->node_list_; n != NULL; n = n->next_) count++;

        //h->circleNodeList();
        node *temp = h->node_list_;
        while (temp->next_ && temp->next_ != h->node_list_) temp = temp->next_;
        temp->next_ = h->node_list_;

        // reduce hole
        node *gmin;
        int min;
        Point r;

        for (; count > limit_; count -= 2) {
            min = MAXINT;

            node *g = h->node_list_;
            do {
                node *g1 = g->next_;
                node *g2 = g1->next_;
                node *g3 = g2->next_;

                if (G::angle(g2, g1, g2, g3) > M_PI) {
                    //	int t = fabs(g3->x_ - g2->x_) + fabs(g2->y_ - g1->y_) + fabs(g3->y_ - g2->y_) + fabs(g2->x_ - g1->x_);	// conditional is boundary length
                    int t = fabs(g3->x_ - g2->x_) * fabs(g2->y_ - g1->y_) + fabs(g3->y_ - g2->y_) * fabs(g2->x_ - g1->x_);    // conditional is area
                    if (t < min) {
                        gmin = g;
                        min = t;
                        r.x_ = g1->x_ + g3->x_ - g2->x_;
                        r.y_ = g1->y_ + g3->y_ - g2->y_;
                    }
                }

                g = g1;
            }
            while (g != h->node_list_);

            if (r == *(gmin->next_->next_->next_->next_)) {
                node *temp = gmin->next_;
                gmin->next_ = gmin->next_->next_->next_->next_->next_;

                delete temp->next_->next_->next_;
                delete temp->next_->next_;
                delete temp->next_;
                delete temp;

                count -= 2;
            }
            else {
                node *newNode = new node();
                newNode->x_ = r.x_;
                newNode->y_ = r.y_;
                newNode->next_ = gmin->next_->next_->next_->next_;

                delete gmin->next_->next_->next_;
                delete gmin->next_->next_;
                delete gmin->next_;

                gmin->next_ = newNode;
            }

            h->node_list_ = gmin;
        }
    }
}

// ------------------------ Dump ------------------------ //

void
GridOfflineAgent::dumpTime() {
    FILE *fp = fopen("Time.tr", "a+");
    fprintf(fp, "%f\n", Scheduler::instance().clock());
    fclose(fp);
}

void
GridOfflineAgent::dumpBoundhole() {
    FILE *fp = fopen("GridOffline.tr", "a+");

    fprintf(fp, "%d - hole\n",my_id_);
    for (polygonHole *p = hole_list_; p != NULL; p = p->next_) {
        node *n = p->node_list_;
        do {
            fprintf(fp, "%f	%f\n", n->x_, n->y_);
            n = n->next_;
        } while (n && n != p->node_list_);

        fprintf(fp, "%f	%f\n\n", p->node_list_->x_, p->node_list_->y_);
    }

    fclose(fp);
}

void
GridOfflineAgent::dumpArea() {
    FILE *fp = fopen("Area.tr", "a+");
    fprintf(fp, "%f\n", G::area(hole_list_->node_list_));
    fclose(fp);
}

void GridOfflineAgent::dumpNeighbor2() {
    FILE *fp = fopen("Neighbor_2.tr", "a+");
    fprintf(fp, "%d	%f	%f	%f	", this->my_id_, this->x_, this->y_, node_->energy_model()->off_time());
    for (node *temp = neighbor_list_; temp; temp = temp->next_)
    {
        fprintf(fp, "%d,", temp->id_);
    }
    fprintf(fp, "\n");
    fclose(fp);
}

void GridOfflineAgent::initTraceFile() {
    FILE *fp;
    fp = fopen("Area.tr", "w");
    fclose(fp);
    fp = fopen("GridOffline.tr", "w");
    fclose(fp);
    fp = fopen("Neighbors.tr", "w");
    fclose(fp);
    fp = fopen("Time.tr", "w");
    fclose(fp);
    fp = fopen("Neighbor_2.tr", "w");
    fclose(fp);
}
