#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "Node.h"
#include "RoutingProtocol.h"
#include "LinkState.h"
#include "DistanceVector.h"
#include <arpa/inet.h>

class RoutingProtocolImpl : public RoutingProtocol {
public:
    RoutingProtocolImpl(Node *n);
    ~RoutingProtocolImpl();

    unsigned short num_ports, router_id;
    eProtocolType protocol_type;

    enum AlarmType {
        ALARM_PORT_ALIVE_CHECK,
        ALARM_PING_PONG_PERIODIC,
        ALARM_DV_ENTRY_ALIVE_CHECK,
        ALARM_LS_ENTRY_ALIVE_CHECK,
        ALARM_DV_PERIODIC_UPDATE,
        ALARM_LS_PERIODIC_UPDATE
    };

    AlarmType alarm_dv_entry_alive_check;
    AlarmType alarm_dv_periodic_update;
    AlarmType alarm_ls_entry_alive_check;
    AlarmType alarm_ls_periodic_update;
    AlarmType alarm_port_alive_check;
    AlarmType alarm_ping_pong_periodic;

    struct NeighborValue {
        unsigned short port;
        unsigned short cost;
        unsigned int   timeStamp;
    };

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // As discussed in the assignment document, your RoutingProtocolImpl is
    // first initialized with the total number of ports on the router,
    // the router's ID, and the protocol type (P_DV or P_LS) that
    // should be used. See global.h for definitions of constants P_DV
    // and P_LS.

    void handle_alarm(void *data);
    // As discussed in the assignment document, when an alarm scheduled by your
    // RoutingProtoclImpl fires, your RoutingProtocolImpl's
    // handle_alarm() function will be called, with the original piece
    // of "data" memory supplied to set_alarm() provided. After you
    // handle an alarm, the memory pointed to by "data" is under your
    // ownership and you should free it if appropriate.

    void recv(unsigned short port, void *packet, unsigned short size);
    // When a packet is received, your recv() function will be called
    // with the port number on which the packet arrives from, the
    // pointer to the packet memory, and the size of the packet in
    // bytes. When you receive a packet, the packet memory is under
    // your ownership and you should free it if appropriate. When a
    // DATA packet is created at a router by the simulator, your
    // recv() function will be called for such DATA packet, but with a
    // special port number of SPECIAL_PORT (see global.h) to indicate
    // that the packet is generated locally and not received from
    // a neighbor router.

private:
    LinkState      ls;
    DistanceVector dv;

    // stores neighbors
    std::unordered_map<unsigned short, NeighborValue> neighborTable;

    Node *sys; // To store Node object; used to access GSR9999 interfaces

    void forwardData(unsigned short port, char *packet, unsigned short size);

    void replyPong(unsigned short port, char *packet, unsigned short size);

    void recvPongMessage(unsigned short port, char *packet, unsigned short size);

    void sendPingToAllPorts();
    
    void checkAlivePorts();

    void recvLSMessage(unsigned short port, char *packet, unsigned short size);
    
    void sendAllLSEntriesToFlood();
    
    void checkAliveLSEntries();

    void recvDVMessage(unsigned short port, char *packet, unsigned short size);
    
    void sendAllDVEntriesToNeighbors();
    
    void checkAliveDVEntries();
};

#endif
