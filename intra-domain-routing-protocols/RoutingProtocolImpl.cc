#include "RoutingProtocolImpl.h"
#include <string.h>

using namespace std;

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
    sys = n;
}

RoutingProtocolImpl::~RoutingProtocolImpl() {}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
    this->num_ports = num_ports;
    this->router_id = router_id;
    this->protocol_type = protocol_type;

    this->alarm_dv_entry_alive_check = ALARM_DV_ENTRY_ALIVE_CHECK;
    this->alarm_dv_periodic_update   = ALARM_DV_PERIODIC_UPDATE;
    this->alarm_ls_entry_alive_check = ALARM_LS_ENTRY_ALIVE_CHECK;
    this->alarm_ls_periodic_update   = ALARM_LS_PERIODIC_UPDATE;
    this->alarm_port_alive_check     = ALARM_PORT_ALIVE_CHECK;
    this->alarm_ping_pong_periodic   = ALARM_PING_PONG_PERIODIC;

    if (protocol_type == P_DV) {
        dv = DistanceVector(router_id);
        sys->set_alarm(this,  1000, &alarm_dv_entry_alive_check);
        sys->set_alarm(this, 30000, &alarm_dv_periodic_update);
    }

    else if (protocol_type == P_LS) {
        ls = LinkState(router_id);
        sys->set_alarm(this,  1000, &alarm_ls_entry_alive_check);
        sys->set_alarm(this, 30000, &alarm_ls_periodic_update);
    }

    sys->set_alarm(this,  1000, &alarm_port_alive_check);
    sys->set_alarm(this, 10000, &alarm_ping_pong_periodic);

    sendPingToAllPorts();
}

void RoutingProtocolImpl::handle_alarm(void *data) {
    switch (*(AlarmType *) data) {

        case ALARM_PORT_ALIVE_CHECK:
            checkAlivePorts();
            sys->set_alarm(this, 1000, data);
            break;

        case ALARM_PING_PONG_PERIODIC:
            sendPingToAllPorts();
            sys->set_alarm(this, 10000, data);
            break;

        case ALARM_DV_ENTRY_ALIVE_CHECK:
            checkAliveDVEntries();
            sys->set_alarm(this, 1000, data);
            break;

        case ALARM_LS_ENTRY_ALIVE_CHECK:
            checkAliveLSEntries();
            sys->set_alarm(this, 1000, data);
            break;

        case ALARM_DV_PERIODIC_UPDATE:
            sendAllDVEntriesToNeighbors();
            sys->set_alarm(this, 30000, data);
            break;

        case ALARM_LS_PERIODIC_UPDATE:
            sendAllLSEntriesToFlood();
            sys->set_alarm(this, 30000, data);
            break;

        default: break;
    }
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
    ePacketType packet_type = (ePacketType) (*(char *) packet);
    switch (packet_type) {

        case DATA:
            forwardData(port, (char *) packet, size);
            break;

        case PING:
            replyPong(port, (char *) packet, size);
            break;

        case PONG:
            recvPongMessage(port, (char *) packet, size);
            break;

        case DV:
            recvDVMessage(port, (char *) packet, size);
            break;

        case LS:
            recvLSMessage(port, (char *) packet, size);
            break;

        default:
            delete[] (char*) packet;
            break;
    }
}

void RoutingProtocolImpl::forwardData(unsigned short port, char *packet, unsigned short size) {

    unsigned short dst = ntohs(*(unsigned short *) (packet + 6));

    // the data has been delivered to destination
    if (this->router_id == dst) {
        delete[] packet;
        return;
    }

    if (this->protocol_type == P_DV) {
        if (dv.forwardTable.find(dst) != dv.forwardTable.end()) {
            unsigned short nextHop = dv.forwardTable[dst];

            sys->send(neighborTable[nextHop].port, packet, size);
        }
        // don't know how to forward data, abandon
        else delete[] packet;
    }
    
    else if (this->protocol_type == P_LS) {
        if (ls.forwardTable.find(dst) != ls.forwardTable.end()) {
            unsigned short nextHop = ls.forwardTable[dst];

            sys->send(neighborTable[nextHop].port, packet, size);
        }
        // don't know how to forward data, abandon
        else delete[] packet;
    }

    // this should never happen
    else delete[] packet;
}

void RoutingProtocolImpl::replyPong(unsigned short port, char *packet, unsigned short size) {
    // move source ID to destination ID
    *(char           *)  packet      = (char) PONG;
    *(unsigned short *) (packet + 6) = *(unsigned short *) (packet + 4);
    *(unsigned short *) (packet + 4) = htons(router_id);
    sys->send(port, packet, size);
}

void RoutingProtocolImpl::recvPongMessage(unsigned short port, char *packet, unsigned short size) {
    unsigned int   currentTime = sys->time();
    unsigned int   sendTime    = ntohl(*(unsigned int   *) (packet + 8));
    unsigned short neighbor_id = ntohs(*(unsigned short *) (packet + 4));

    unsigned short cost = currentTime - sendTime;
    unsigned short prev = (neighborTable.find(neighbor_id) != neighborTable.end()) ? 
        neighborTable[neighbor_id].cost : 0;

    neighborTable[neighbor_id].port      = port;
    neighborTable[neighbor_id].cost      = cost;
    neighborTable[neighbor_id].timeStamp = currentTime;

    if (this->protocol_type == P_DV) {
        // update distance vector table
        for (auto &dstEntry : dv.distTable) {
            auto &nextHopTuple = dstEntry.second;
            if (nextHopTuple.first == neighbor_id) {
                nextHopTuple.second.cost     += cost;
                nextHopTuple.second.cost     -= prev;
                nextHopTuple.second.timeStamp = currentTime;
            }
        }

        // update neighbor itself
        auto dstIter = dv.distTable.find(neighbor_id);
        if (dstIter == dv.distTable.end() || cost < dstIter->second.second.cost) {
            dv.distTable[neighbor_id].first            = neighbor_id;
            dv.distTable[neighbor_id].second.cost      = cost;
            dv.distTable[neighbor_id].second.timeStamp = currentTime;
        }

        if (dv.updateForwardTable())
            sendAllDVEntriesToNeighbors();
    }

    else if (this->protocol_type == P_LS) {
        // update link state table
        if (ls.linkTable[router_id].find(neighbor_id) == ls.linkTable[router_id].end() || 
            ls.linkTable[router_id][neighbor_id].cost != cost) {
            
            ls.linkTable[router_id][neighbor_id].cost = cost;
            ls.updateForwardTable();
            sendAllLSEntriesToFlood();
        }
        ls.linkTable[router_id][neighbor_id].timeStamp = currentTime;
    }

    delete[] packet;
}

void RoutingProtocolImpl::checkAlivePorts() {
    unsigned int currentTime = sys->time();

    vector<unordered_map<unsigned short, NeighborValue>::iterator> evict;
    for (auto iter = neighborTable.begin(); iter != neighborTable.end(); ++iter) {

        if (currentTime - iter->second.timeStamp >= 15000)
            evict.emplace_back(iter);
    }

    for (auto iter : evict)
        neighborTable.erase(iter);

    if (this->protocol_type == P_DV) {
        // check if neighbor removal affect the distance table
        vector<unordered_map<unsigned short, pair<unsigned short, DistanceVector::DVValue>>::iterator> evict;
        for (auto dstIter = dv.distTable.begin(); dstIter != dv.distTable.end(); ++dstIter) {

            auto &nextHopTuple = dstIter->second;
            if (neighborTable.find(nextHopTuple.first) == neighborTable.end())
                evict.emplace_back(dstIter);
        }

        for (auto dstIter : evict)
            dv.distTable.erase(dstIter);

        if (dv.updateForwardTable())
            sendAllDVEntriesToNeighbors();
    }

    else if (this->protocol_type == P_LS) {
        bool isModified = false;

        vector<unordered_map<unsigned short, LinkState::LSValue>::iterator> evict;
        for (auto iter = ls.linkTable[router_id].begin(); iter != ls.linkTable[router_id].end(); ++iter) {
            
            if (neighborTable.find(iter->first) == neighborTable.end()) {
                isModified = true;
                evict.emplace_back(iter);
            }
        }

        for (auto iter : evict)
            ls.linkTable[router_id].erase(iter);

        if (isModified) {
            ls.updateForwardTable();
            sendAllLSEntriesToFlood();
        }
    }
}

void RoutingProtocolImpl::sendPingToAllPorts() {
    for (int i = 0; i < num_ports; i++) {
        char *packet = new char[12];
        *(char           *)  packet      = (char) PING;         // packet type
        *(unsigned short *) (packet + 2) = htons(12);           // size
        *(unsigned short *) (packet + 4) = htons(router_id);    // source id
        *(unsigned short *) (packet + 6) = htons(0);            // destination id (not used for PING packet)
        *(unsigned int   *) (packet + 8) = htonl(sys->time());  // PING time
        sys->send(i, packet, 12);
    }
}

void RoutingProtocolImpl::recvLSMessage(unsigned short port, char *packet, unsigned short size) {
    unsigned short holder = ntohs(*(unsigned short *) (packet + 4));
    unsigned int sequence = ntohl(*(unsigned int   *) (packet + 8));

    unsigned int currentTime = sys->time();

    // if holder is the router itself, ignore
    if (holder == router_id) {
        delete[] packet;
        return;
    }

    // if this message has been received before, ignore
    if (ls.seqTable.find(holder) != ls.seqTable.end() && sequence <= ls.seqTable[holder]) {
        delete[] packet;
        return;
    }

    ls.seqTable[holder] = sequence;

    // initialize the holder row entry of link state table
    auto prevTableRow = ls.linkTable[holder];
    ls.linkTable[holder].clear();

    for (int i = 12; i < size; i += 4) {
        unsigned short neighbor = ntohs(*(unsigned short *) (packet + i));
        unsigned short cost     = ntohs(*(unsigned short *) (packet + i + 2));

        ls.linkTable[holder][neighbor].cost      = cost;
        ls.linkTable[holder][neighbor].timeStamp = currentTime;
    }

    for (const auto &sendTo : neighborTable) {
        if (sendTo.second.port != port) {
            char *packetNew = new char[size];
            memcpy(packetNew, packet, size);
            sys->send(sendTo.second.port, packetNew, size);
        }
    }

    delete[] packet;

    if (!equal(prevTableRow, ls.linkTable[holder]))
        ls.updateForwardTable();
}

void RoutingProtocolImpl::sendAllLSEntriesToFlood() {

    int size = neighborTable.size() * 4 + 12;

    for (const auto &sendTo : neighborTable) {
        char *packet = new char[size];

        *(char           *)  packet      = (char) LS;
        *(unsigned short *) (packet + 2) = htons(size);
        *(unsigned short *) (packet + 4) = htons(ls.self);
        *(unsigned short *) (packet + 6) = htons(sendTo.first);
        *(unsigned int   *) (packet + 8) = htonl(ls.sequence);
        
        int ptr = 12;
        for (const auto &entry : neighborTable) {
            *(unsigned short *) (packet + ptr)     = htons(entry.first);
            *(unsigned short *) (packet + ptr + 2) = htons(entry.second.cost);
            ptr += 4;
        }

        sys->send(sendTo.second.port, packet, size);
    }

    ls.sequence++;
}

void RoutingProtocolImpl::checkAliveLSEntries() {

    unsigned int currentTime = sys->time();
    bool isModified = false;

    vector<unordered_map<unsigned short, unordered_map<unsigned short, LinkState::LSValue>>::iterator> evict;
    for (auto rowIter = ls.linkTable.begin(); rowIter != ls.linkTable.end(); ++rowIter) {

        vector<unordered_map<unsigned short, LinkState::LSValue>::iterator> innerEvict;
        for (auto entryIter = rowIter->second.begin(); entryIter != rowIter->second.end(); ++entryIter) {

            if (currentTime - entryIter->second.timeStamp >= 45000) {
                isModified = true;
                innerEvict.emplace_back(entryIter);
            }
        }

        for (auto entryIter : innerEvict)
                rowIter->second.erase(entryIter);

        if (rowIter->second.empty())
            evict.emplace_back(rowIter);
    }

    for (auto rowIter : evict)
        ls.linkTable.erase(rowIter);

    if (isModified)
        ls.updateForwardTable();
}

void RoutingProtocolImpl::recvDVMessage(unsigned short port, char *packet, unsigned short size) {
    unsigned short holder = ntohs(*(unsigned short *) (packet + 4));

    unsigned int currentTime = sys->time();

    // ignore if there is no entry for holder in neighborTable
    if (neighborTable.find(holder) == neighborTable.end()) {
        delete[] packet;
        return;
    }

    unsigned short instantCost = neighborTable[holder].cost;

    // store the current reachable nodes from holder
    unordered_set<unsigned short> inReach;

    for (int i = 8; i < size; i += 4) {
        unsigned short node = ntohs(*(unsigned short *) (packet + i));
        unsigned short cost = ntohs(*(unsigned short *) (packet + i + 2));

        // skip the node out of reach
        if (cost == INFINITY_COST)
            continue;

        // skip the situation where the dst node is itself
        if (node == dv.self)
            continue;

        inReach.insert(node);

        cost += instantCost;

        auto dstIter = dv.distTable.find(node);

        // if the holder is the next hop, modify the current cost
        if (dstIter != dv.distTable.end() && holder == dstIter->second.first) {
            dv.distTable[node].second.cost      = cost;
            dv.distTable[node].second.timeStamp = currentTime;
        }

        // if the holder is not the next hop, choose a shorter one
        else if (dstIter == dv.distTable.end() || cost < dstIter->second.second.cost) {
            dv.distTable[node].first            = holder;
            dv.distTable[node].second.cost      = cost;
            dv.distTable[node].second.timeStamp = currentTime;
        }
    }

    vector<unordered_map<unsigned short, pair<unsigned short, DistanceVector::DVValue>>::iterator> evict;
    for (auto dstIter = dv.distTable.begin(); dstIter != dv.distTable.end(); ++dstIter) {

        // if the current shortest path comes through holder, but holder cannot provide such path now
        auto &nextHopTuple = dstIter->second;
        if (nextHopTuple.first == holder && inReach.find(dstIter->first) == inReach.end())
            evict.emplace_back(dstIter);
    }

    for (auto dstIter : evict)
        dv.distTable.erase(dstIter);

    delete[] packet;

    if (dv.updateForwardTable())
        sendAllDVEntriesToNeighbors();
}

void RoutingProtocolImpl::sendAllDVEntriesToNeighbors() {
    
    int size = (dv.distTable.size() + 1) * 4 + 8;
    
    for (const auto &sendTo : neighborTable) {

        char *packet = new char[size];

        *(char           *)  packet      = (char) DV;
        *(unsigned short *) (packet + 2) = htons(size);
        *(unsigned short *) (packet + 4) = htons(dv.self);
        *(unsigned short *) (packet + 6) = htons(sendTo.first);

        *(unsigned short *) (packet +  8) = htons(dv.self);
        *(unsigned short *) (packet + 10) = htons(0);

        int ptr = 12;
        for (const auto &entry : dv.distTable) {
            // the node we can reach, not neccessarily neighbor
            *(unsigned short *) (packet + ptr) = htons(entry.first);

            auto &nextHopTuple = entry.second;
            unsigned short nextHop = nextHopTuple.first;

            // poison reverse: if next hop is the node we are going to send DV message to, set cost to INFINITY
            if (nextHop == sendTo.first)
                *(unsigned short *) (packet + ptr + 2) = htons(INFINITY_COST);
            
            // only send the shorest path
            else {
                auto &dvValue = nextHopTuple.second;
                *(unsigned short *) (packet + ptr + 2) = htons(dvValue.cost);
            }

            ptr += 4;
        }

        sys->send(sendTo.second.port, packet, size);
    }
}

void RoutingProtocolImpl::checkAliveDVEntries() {

    unsigned int currentTime = sys->time();

    vector<unordered_map<unsigned short, pair<unsigned short, DistanceVector::DVValue>>::iterator> evict;
    for (auto dstIter = dv.distTable.begin(); dstIter != dv.distTable.end(); ++dstIter) {

        auto &nextHopTuple = dstIter->second;
        if (currentTime - nextHopTuple.second.timeStamp >= 45000)
            evict.emplace_back(dstIter);
    }

    for (auto dstIter : evict)
        dv.distTable.erase(dstIter);

    if (dv.updateForwardTable())
        sendAllDVEntriesToNeighbors();
}
