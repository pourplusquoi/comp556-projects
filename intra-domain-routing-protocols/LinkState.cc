#include "LinkState.h"

using namespace std;

bool LinkState::LSValue::operator==(const LSValue &rhs) const {
    return this->cost == rhs.cost;
}

bool equal(const unordered_map<unsigned short, LinkState::LSValue> &rowA, 
    const unordered_map<unsigned short, LinkState::LSValue> &rowB) {
    
    if (rowA.size() != rowB.size())
        return false;
    for (auto iter = rowA.begin(); iter != rowA.end(); ++iter) {
        auto it = rowB.find(iter->first);
        if (it == rowB.end() || !(iter->second == it->second))
            return false;
    }
    return true;
}

void LinkState::dijkstra() {
    // holds results of dijkstra algorithm
    optimal = unordered_map<unsigned short, pair<unsigned short, unsigned short>>();

    // holds the whole collection of nodes
    unordered_map<unsigned short, pair<unsigned short, unsigned short>> pool;

    for (auto &node : linkTable)
        for (auto &link : node.second)
            pool[link.first] = make_pair(0xffff, 0xffff);
    pool[self] = make_pair(0, self);

    while (true) {
        unsigned short low = 0xffff, opt = 0xffff, pre = 0xffff;
        for (auto &routerTuple : pool) {
            unsigned short curRouter = routerTuple.first;
            unsigned short cost      = routerTuple.second.first;
            unsigned short prevHop   = routerTuple.second.second;

            if (cost < low && optimal.find(curRouter) == optimal.end()) {
                low = cost;
                opt = curRouter;
                pre = prevHop;
            }
        }

        // all the remaining nodes are out of reach
        if (opt == 0xffff)
            break;

        optimal[opt] = make_pair(low, pre);
        for (auto &link : linkTable[opt]) {
            unsigned short newCost = low + link.second.cost;
            if (newCost < pool[link.first].first)
                pool[link.first] = make_pair(newCost, opt);
        }
    }
}

void LinkState::updateForwardTable() {
    // holds all reachable node
    unordered_set<unsigned short> nodes;
    for (auto &node : linkTable)
        for (auto &link : node.second)
            nodes.insert(link.first);

    dijkstra();

    forwardTable.clear();

    for (const auto &node : nodes) {
        // avoid forwarding self
        if (node == self)
            continue;
        
        unsigned short next = nextHop(node);
        if (next != 0xffff)
            forwardTable[node] = next;
    }
}

unsigned short LinkState::nextHop(unsigned short node) {
    // if the node is not reachable, return INFINITY (0xffff)
    if (optimal.find(node) == optimal.end())
        return 0xffff;

    while (optimal[node].second != self)
        node = optimal[node].second;
    
    return node;
}
