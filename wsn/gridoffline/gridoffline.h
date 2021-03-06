/*
 * grid.h
 *
 * Created on: Mar 20, 2014
 * author :    trongnguyen
 */

#ifndef GRIDOFFLINE_H_
#define GRIDOFFLINE_H_

#include "wsn/geomathhelper/geo_math_helper.h"
#include "wsn/gpsr/gpsr.h"
#include "grid_packet.h"

class GridOfflineAgent;

struct stuckangle
{
	// two neighbors that create stuck angle with node.
	node *a_;
	node *b_;

	stuckangle *next_;
};

struct polygonHole
{
	int hole_id_;
	struct node* node_list_;
	struct polygonHole* next_;

	~polygonHole()
	{
		node* temp = node_list_;
		do {
			delete temp;
			temp = temp->next_;
		} while(temp && temp != node_list_);
	}

	void circleNodeList()
	{
		node* temp = node_list_;
		while (temp->next_ && temp->next_ != node_list_) temp = temp->next_;
		temp->next_ = node_list_;
	}
};

typedef void(GridOfflineAgent::*firefunction)(void);

class GridOfflineTimer : public TimerHandler
{
	public:
		GridOfflineTimer(GridOfflineAgent *a, firefunction f) : TimerHandler() {
			a_ = a;
			firing_ = f;
		}

	protected:
		virtual void expire(Event *e);
		GridOfflineAgent *a_;
		firefunction firing_;
};

class GridOfflineAgent : public GPSRAgent {
private:
	friend class GridOfflineHelloTimer;
	GridOfflineTimer findStuck_timer_;
	GridOfflineTimer grid_timer_;

	double range_;
	double limit_;
//	double r_;
//	int limit_boundhole_hop_;
//
//	stuckangle* stuck_angle_;
//	polygonHole* hole_list_;

	void findStuckAngle();

	//void sendGridOffline(polygonHole* h);
	//void recvGridOffline(Packet* p);

	void reducePolygonHole(polygonHole* h);

protected:
    double r_;
    int limit_boundhole_hop_;
    stuckangle* stuck_angle_;
    polygonHole* hole_list_;

    node* getNeighborByBoundhole(Point*, Point*);

    void createPolygonHole(Packet*);

    void dumpTime();
    void dumpBoundhole();
    void dumpArea();

    virtual void addData(Packet*);
    virtual void sendBoundHole();
    virtual void recvBoundHole(Packet*);
    virtual void startUp();

public:
	GridOfflineAgent();
	int 	command(int, const char*const*);
	void 	recv(Packet*, Handler*);

public:
    virtual char const * getAgentName();

    virtual void initTraceFile();

	void dumpNeighbor2();
};

#endif /* GRID_H_ */
