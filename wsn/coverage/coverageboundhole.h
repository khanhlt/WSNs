//
// Created by eleven on 9/10/15.
//

#ifndef NS_CONVERAGEHOLE_H
#define NS_CONVERAGEHOLE_H

#include "config.h"

#include "agent.h"
#include "timer-handler.h"
#include "mobilenode.h"
#include "classifier-port.h"
#include "cmu-trace.h"

#include "../gpsr/gpsr.h"
#include "../boundhole/boundhole.h"
#include "coverageboundhole_packet.h"
#include "node.h"

#include "wsn/geomathhelper/geo_math_helper.h"

#define C_RED     2
#define C_WHITE   1
#define C_BLACK   0

class CoverageBoundHoleAgent;

struct sensor_neighbor : neighbor{
    Point i1_; // intersects
    Point i2_;
};

struct list {
    int e_;
    struct list* next_;
};

typedef void(CoverageBoundHoleAgent::*fire)(void);

class CoverageBoundHoleTimer : public TimerHandler
{
public:
    CoverageBoundHoleTimer(Agent *a, fire f) : TimerHandler() {
        a_ = a;
        firing_ = f;
    }

protected:
    virtual void expire(Event *e);
    Agent *a_;
    fire firing_;
};

class CoverageBoundHoleAgent : public GPSRAgent{
private:
    friend class BoundHoleHelloTimer;
    CoverageBoundHoleTimer boundhole_timer_;

    bool isBoundary = false;

    void startUp();

    bool boundaryNodeDetection();
    void holeBoundaryDetection();

    void recvCoverage(Packet*);

    void reducePolygonHole(polygonHole*);

    void dumpSensorNeighbor();
    void dumpBoundaryDetect();
protected:
    double communication_range_;
    double sensor_range_;
    int limit_hop_;
    sensor_neighbor *sensor_neighbor_list_; // list of the neighbors by sensor range, fixed: sensor range = 1/2*communication range
    polygonHole *hole_list_;
    stuckangle *cover_neighbors_; // pair of neighbors make with node to create a fragment of hole boundary

    void addSensorNeighbor(nsaddr_t, Point, int);
    sensor_neighbor* getSensorNeighbor(nsaddr_t);
    void addNeighbor(nsaddr_t, Point); // override from GPSRAgent
    node* getNextSensorNeighbor(nsaddr_t prev_node);

    void gridConstruction(polygonHole *);
    void patchingHole(polygonHole *, int, int, bool**, int, int);
public:
    CoverageBoundHoleAgent();

    int  command(int, const char*const*);
    void recv(Packet*, Handler*);

    void dumpCoverageBoundHole(polygonHole *pHole);
};

#endif //NS_CONVERAGEHOLE_H
