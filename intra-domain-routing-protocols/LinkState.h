#ifndef LINKSTATE_H
#define LINKSTATE_H

#include <unordered_set>
#include <unordered_map>

class LinkState {
public:

    struct LSValue {

        unsigned short cost;

        /** last update time stamp **/
        unsigned int   timeStamp;

        bool operator==(const LSValue &rhs) const;
    };

    /** id of node itself **/
    unsigned short self;

    /** curent sequence number of itself **/
    unsigned int sequence;

    std::unordered_map<unsigned short, std::unordered_map<unsigned short, LSValue>> linkTable;

    std::unordered_map<unsigned short, unsigned int> seqTable;

    // stores next hop
    std::unordered_map<unsigned short, unsigned short> forwardTable;

    LinkState() {}
    LinkState(unsigned short number) : self(number), sequence(1) {}
    ~LinkState() {}

    /** figure out the current shortest paths for all nodes **/
    void updateForwardTable();


private:

    /** @key: vertex
        @value: (cost, parent vertex) **/
    std::unordered_map<unsigned short, std::pair<unsigned short, unsigned short>> optimal;

    void dijkstra();

    /** compute the next hop using results of dijkstra
        @param: id of target node
        @return: next hop id **/
    unsigned short nextHop(unsigned short);
};

bool equal(const std::unordered_map<unsigned short, LinkState::LSValue>&, 
    const std::unordered_map<unsigned short, LinkState::LSValue>&);

#endif
