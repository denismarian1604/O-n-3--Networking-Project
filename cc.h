// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-        


#ifndef CC_H
#define CC_H

/*
 * An CC source and sink
 */

#include <list>
#include <map>
//#include "util.h"
#include "math.h"
#include "config.h"
#include "network.h"
#include "ccpacket.h"
#include "queue.h"
#include "eventlist.h"

#define timeInf 0

class CCSink;

class CCSrc :  public PacketSink, public EventSource  {
    friend class CCSink;
public:
    CCSrc(EventList &eventlist);

    virtual void connect(Route* routeout, Route* routeback, CCSink& sink, simtime_picosec startTime);

    void startflow();

    virtual void doNextEvent();
    virtual void receivePacket(Packet& pkt);

    virtual void processAck(const CCAck& ack);
    virtual void processNack(const CCNack& nack);
    
    // should really be private, but loggers want to see:
    uint64_t _highest_sent;  //seqno is in bytes
    uint64_t _packets_sent;
    uint64_t _flightsize,_next_decision, _ssthresh;
    double _cwnd;
    uint32_t _acks_received;
    uint32_t _nacks_received;

    uint32_t _wmax;
    uint32_t _wmax_last;
    uint32_t _wtcp;
    uint32_t _origin_point;
    double _K;
    double _C;
    double _beta;
    uint32_t _cnt;
    uint32_t _cwnd_cnt;
    uint32_t _ack_cnt;

    double _epoch_start;
    double _dMin;

    bool tcp_friendliness;
    bool fast_convergence;

    uint64_t base_rtt;
    uint64_t min_rtt;
    uint64_t rtt;

    map<CCPacket::seq_t, simtime_picosec> _sent_times; // Track sent times for packets

    int cubic_update();

    void cubic_tcp_friendliness();

    void cubic_reset();

    void check_for_timeouts();
    void process_timeout(CCPacket::seq_t seqno);

    void print_stats();

    uint16_t _mss;
    uint32_t _drops;

    CCSink* _sink;
 
    const Route* _route;

    void send_packet();

    virtual const string& nodename() { return _nodename; }
    inline uint32_t flow_id() const { return _flow.flow_id();}
 
private:
    // Connectivity
    PacketFlow _flow;
    string _nodename;
    uint32_t _node_num;
    bool _flow_started;

    static int _global_node_count;
};

class CCSink : public PacketSink, public DataReceiver, public Logged {
    friend class CCSrc;
public:
    CCSink();

    virtual void receivePacket(Packet& pkt);

    virtual uint64_t cumulative_ack() {return _total_received;};
    virtual uint32_t drops() {return 0;};
    
    uint32_t _drops;
    uint64_t total_received() const { return _total_received;}
    virtual const string& nodename() { return _nodename; }

    uint32_t get_id() {return _src->flow_id();} 

    uint64_t get_cwnd() {return _src->_cwnd;}
    CCSrc* _src;

private:
    // Connectivity
    void connect(CCSrc& src, Route* route);
    const Route* _route;

    string _nodename;
    uint64_t _total_received;
 
    // Mechanism
    void send_ack(simtime_picosec ts, CCPacket::seq_t ackno, bool ecn);
    void send_nack(simtime_picosec ts, CCPacket::seq_t ackno);
};


#endif
