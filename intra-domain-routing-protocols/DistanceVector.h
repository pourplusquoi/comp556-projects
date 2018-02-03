#ifndef DISTANCEVECTOR_H
#define DISTANCEVECTOR_H

#include <unordered_map>

class DistanceVector {
public:

    struct DVValue {
        /** total cost to reach this vertex **/
        unsigned short cost;

        /** last update time stamp **/
        unsigned int timeStamp;
    };

    /** id of node itself **/
    unsigned short self;

    std::unordered_map<unsigned short, std::pair<unsigned short, DVValue>> distTable;

    // stores next hop
    std::unordered_map<unsigned short, unsigned short> forwardTable;

    DistanceVector() {}
    DistanceVector(unsigned short number) : self(number) {}
    ~DistanceVector() {}

    /** figure out the current shortest paths for all nodes **/
    bool updateForwardTable();
};

#endif