// -*- c-basic-offset: 4; indent-tabs-mode: nil -*- 
#include <math.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include "cc.h"
#include "queue.h"
#include <stdio.h>
#include "switch.h"
#include "ecn.h"
using namespace std;

////////////////////////////////////////////////////////////////
//  CC SOURCE. Aici este codul care ruleaza pe transmitatori. Tot ce avem nevoie pentru a implementa
//  un algoritm de congestion control se gaseste aici.
////////////////////////////////////////////////////////////////
int CCSrc::_global_node_count = 0;

CCSrc::CCSrc(EventList &eventlist)
    : EventSource(eventlist,"cc"), _flow(NULL)
{
    _mss = Packet::data_packet_size();
    _acks_received = 0;
    _nacks_received = 0;

    _highest_sent = 0;
    _next_decision = 0;
    _flow_started = false;
    _sink = 0;

    _wmax = 0;
    _wmax_last = 0;
    _epoch_start = 0;
    _origin_point = 0;
    _dMin = 0;
    _wtcp = 0;
    _K = 0;
    _ack_cnt = 0;

    _beta = 0.5;
    _C = 0.2;

    fast_convergence = true;
    tcp_friendliness = true;
  
    _node_num = _global_node_count++;
    _nodename = "CCsrc " + to_string(_node_num);

    _cwnd = 10 * _mss;
    _ssthresh = 0xFFFFFFFFFF;
    _flightsize = 0;
    _flow._name = _nodename;
    setName(_nodename);
}

/* Porneste transmisia fluxului de octeti */
void CCSrc::startflow(){
    cout << "Start flow " << _flow._name << " at " << timeAsSec(eventlist().now()) << "s" << endl;
    _flow_started = true;
    _highest_sent = 0;
    _packets_sent = 0;

    while (_flightsize + _mss < _cwnd)
        send_packet();
}

/* Initializeaza conexiunea la host-ul sink */
void CCSrc::connect(Route* routeout, Route* routeback, CCSink& sink, simtime_picosec starttime) {
    assert(routeout);
    _route = routeout;
    
    _sink = &sink;
    _flow._name = _name;
    _sink->connect(*this, routeback);

    eventlist().sourceIsPending(*this,starttime);
}


/* Variabilele cu care vom lucra:
    _nacks_received
    _flightsize -> numarul de bytes aflati in zbor
    _mss -> maximum segment size
    _next_decision 
    _highest_sent
    _cwnd
    _ssthresh
    
    CCAck._ts -> timestamp ACK
    eventlist.now -> timpul actual
    eventlist.now - CCAck._tx -> latency
    
    ack.ackno();
    
    > Puteti include orice alte variabile in restul codului in functie de nevoie.
*/
/* TODO: In mare parte aici vom avea implementarea algoritmului si in functie de nevoie in celelalte functii */

void CCSrc::cubic_tcp_friendliness() {
    _wtcp = _wtcp + ((3 * _beta) / (2 - _beta)) * (_ack_cnt / _cwnd);
    _ack_cnt = 0;
    if (_wtcp > _cwnd) {
        int max_cnt = _cwnd / (_wtcp - _cwnd);
        if (_cnt > max_cnt) _cnt = max_cnt;
    }
}

int CCSrc::cubic_update() {
    double tcp_time_stamp = eventlist().now() / 1e9;

    _ack_cnt++;

    if (_epoch_start == 0) {
        _epoch_start = tcp_time_stamp;
        if (_cwnd < _wmax_last) {
            _K = 1.1 * std::cbrt((_wmax_last - _cwnd) / _C);
            _origin_point = _wmax_last;
        } else {
            _K = 0;
            _origin_point = _cwnd;
        }
        _ack_cnt = 1;
        _wtcp = _cwnd;
    }

    double t = tcp_time_stamp + _dMin - _epoch_start;
    double target = _origin_point + _C * std::pow(t - _K, 3);

    if (target > _cwnd) {
        _cnt = (_cwnd / (target - _cwnd));
    } else {
        _cnt = 100 * _cwnd; // Large value to prevent increase
    }

    if (tcp_friendliness) {
        cubic_tcp_friendliness();
    }

    return _cnt;
}


//Aceasta functie este apelata atunci cand dimensiunea cozii a fost depasita iar packetul cu numarul de secventa ackno a fost aruncat.
void CCSrc::processNack(const CCNack& nack){    
    //cout << "CC " << _name << " got NACK " <<  nack.ackno() << _highest_sent << " at " << timeAsMs(eventlist().now()) << " us" << endl;    
    _nacks_received ++;    
    _flightsize -= _mss;    
    
    if (nack.ackno()>=_next_decision) {  

        _epoch_start = 0;

        if (_cwnd < _wmax_last && fast_convergence) {
            _wmax_last = _cwnd * (2 - _beta) / 2.0;
        } else {
            _wmax_last = _cwnd;
        }

        _cwnd *= (1 - _beta);
        if (_cwnd < _mss)    
            _cwnd = _mss;    
    
        _ssthresh = _cwnd;
    
        _next_decision = _highest_sent + _cwnd;    
    }    
}  

// std::ofstream fout("log.txt");
    
/* Process an ACK.  Mostly just housekeeping*/    
void CCSrc::processAck(const CCAck& ack) {    
    CCAck::seq_t ackno = ack.ackno();    
    
    _acks_received++;    
    _flightsize -= _mss;

    if (ack.is_ecn_marked()){
        if (ack.ackno()>=_next_decision) {  
            _epoch_start = 0;

            if (_cwnd < _wmax_last && fast_convergence) {
                _wmax_last = _cwnd * (2 - _beta) / 2.0;
            } else {
                _wmax_last = _cwnd;
            }

            _cwnd *= (1 - _beta / 2);
            if (_cwnd < _mss)    
                _cwnd = _mss;    
        
            _ssthresh = _cwnd;
        
            _next_decision = _highest_sent + _cwnd;    
        }
        return;
    }

    double RTT = (eventlist().now() - ack.ts()) / 1e9; // is this RTT?

    if (_dMin > 0) {
        _dMin = std::min(_dMin, RTT);
    } else {
        _dMin = RTT;
    }  

    if (_cwnd <= _ssthresh) {
        // Slow start phase
        _cwnd += _mss;
    } else {
        // Congestion avoidance phase
        _cnt = cubic_update();

        if (_cwnd_cnt > _cnt) {
            _cwnd += _mss;
            _cwnd_cnt = 0;
        } else {
            _cwnd_cnt += 1;
        }
    }
}    


/* Functia de receptie, in functie de ce primeste cheama processLoss sau processACK */
void CCSrc::receivePacket(Packet& pkt) 
{
    if (!_flow_started){
        return; 
    }

    switch (pkt.type()) {
    case CCNACK: 
        processNack((const CCNack&)pkt);
        pkt.free();
        break;
    case CCACK:
        processAck((const CCAck&)pkt);
        pkt.free();
        break;
    default:
        cout << "Got packet with type " << pkt.type() << endl;
        abort();
    }

    //now send packets!
    while (_flightsize + _mss < _cwnd)
        send_packet();
}

// Note: the data sequence number is the number of Byte1 of the packet, not the last byte.
/* Functia care se este chemata pentru transmisia unui pachet */
void CCSrc::send_packet() {
    CCPacket* p = NULL;

    assert(_flow_started);

    p = CCPacket::newpkt(*_route,_flow, _highest_sent+1, _mss, eventlist().now());
    
    _highest_sent += _mss;
    _packets_sent++;

    _flightsize += _mss;

    //cout << "Sent " << _highest_sent+1 << " Flow Size: " << _flow_size << " Flow " << _name << " time " << timeAsUs(eventlist().now()) << endl;
    p->sendOn();
}

void CCSrc::doNextEvent() {
    if (!_flow_started){
      startflow();
      return;
    }
}

////////////////////////////////////////////////////////////////
//  CC SINK Aici este codul ce ruleaza pe receptor, in mare nu o sa aducem multe modificari
////////////////////////////////////////////////////////////////

/* Only use this constructor when there is only one for to this receiver */
CCSink::CCSink()
    : Logged("CCSINK"), _total_received(0) 
{
    _src = 0;
    
    _nodename = "CCsink";
    _total_received = 0;
}

/* Connect a src to this sink. */ 
void CCSink::connect(CCSrc& src, Route* route)
{
    _src = &src;
    _route = route;
    setName(_src->_nodename);
}


// Receive a packet.
// seqno is the first byte of the new packet.
void CCSink::receivePacket(Packet& pkt) {
    switch (pkt.type()) {
    case CC:
        break;
    default:
        abort();
    }

    CCPacket *p = (CCPacket*)(&pkt);
    CCPacket::seq_t seqno = p->seqno();

    simtime_picosec ts = p->ts();
    //bool last_packet = ((CCPacket*)&pkt)->last_packet();

    if (pkt.header_only()){
        send_nack(ts,seqno);      
    
        p->free();

        //cout << "Wrong seqno received at CC SINK " << seqno << " expecting " << _cumulative_ack << endl;
        return;
    }

    int size = p->size()-ACKSIZE; 
    _total_received += Packet::data_packet_size();;

    bool ecn = (bool)(pkt.flags() & ECN_CE);

    send_ack(ts,seqno,ecn);
    // have we seen everything yet?
    pkt.free();
}

void CCSink::send_ack(simtime_picosec ts,CCPacket::seq_t ackno,bool ecn) {
    CCAck *ack = 0;
    ack = CCAck::newpkt(_src->_flow, *_route, ackno,ts,ecn);
    ack->sendOn();
}

void CCSink::send_nack(simtime_picosec ts, CCPacket::seq_t ackno) {
    CCNack *nack = NULL;
    nack = CCNack::newpkt(_src->_flow, *_route, ackno,ts);
    nack->sendOn();
}
