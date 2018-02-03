#include "DistanceVector.h"

using namespace std;

bool equal(const unordered_map<unsigned short, unsigned short> &tableA, 
    const unordered_map<unsigned short, unsigned short> &tableB) {
    
    if (tableA.size() != tableB.size())
        return false;
    for (auto &entry : tableA) {
        auto iter = tableB.find(entry.first);
        if (iter == tableB.end() || iter->second != entry.second)
            return false;
    }
    return true;
}

bool DistanceVector::updateForwardTable() {

    auto previousTable = forwardTable;

    forwardTable.clear();

    for (const auto &dstEntry : distTable) {

        auto &nextHopTuple = dstEntry.second;
        forwardTable[dstEntry.first] = nextHopTuple.first;
    }

    return !equal(previousTable, forwardTable);
}