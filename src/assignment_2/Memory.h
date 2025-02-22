#ifndef MEMORY_H
#define MEMORY_H
#define SC_ALLOW_DEPRECATED_IEEE_API

#include <iostream>
#include <systemc.h>
#include <queue>

#include "bus_slave_if.h"
#include "helpers.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you nee

static bool test = true;

static sc_time totalaqtime = sc_time_stamp();
static int totalwritereq = 0;
static int totalreadreq = 0;

struct request {
    uint64_t addr; 
    Function func; 
    uint64_t trans_id;
    uint64_t cache_id;
};

class Memory : public bus_slave_if, public sc_module {
    public:
    
    sc_in<bool> Port_CLK;
    // Connections to caches
    std::vector<sc_out<Function>*> Port_BusCacheFunc;
    std::vector<sc_out<uint64_t>*> Port_BusCacheAddr;
    std::vector<sc_out<uint64_t>*> Port_BusCacheTransId;

    queue<request> request_queue;
    std::vector<int64_t> cache_list;
    bool EOF_CPU = false;
    
    SC_CTOR(Memory) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        Port_BusCacheFunc.resize(NUM_CPUS);
        Port_BusCacheAddr.resize(NUM_CPUS);
        Port_BusCacheTransId.resize(NUM_CPUS);

        for (int i = 0; i < NUM_CPUS; i++) {
            Port_BusCacheFunc[i] = new sc_out<Function>();  
            Port_BusCacheAddr[i] = new sc_out<uint64_t>();
            Port_BusCacheTransId[i] = new sc_out<uint64_t>();
        }
    }

    ~Memory() {
        // nothing to do here right now.
    }

    void execute() {
        request req;
        while (true) {
            wait(Port_CLK.value_changed_event());

            // Select newest request
            if(!request_queue.empty()) {
                req = request_queue.front(); 
                request_queue.pop();
            }

            // Broadcast the request for snooping 
            for(int i = 0; i < NUM_CPUS; i++) {
                Port_BusCacheTransId[i]->write(req.trans_id);
                Port_BusCacheAddr[i]->write(req.addr);
                Port_BusCacheFunc[i]->write(req.func);
            } 
    
        }
    }

    void read(uint64_t addr, uint64_t trans_id, uint64_t cache_id) {
        assert((addr & 0x3) == 0);
        totalreadreq += 1;
        log(name(), "       received read request for addr", addr);
        request_queue.push((request) {.addr = addr, .func = FUNC_READ, .trans_id = trans_id, .cache_id = cache_id});
    }

    void write(uint64_t addr, uint64_t trans_id, uint64_t cache_id) {
        assert((addr & 0x3) == 0);
        totalwritereq += 1;
        log(name(), "       received write request for addr", addr);
        request_queue.push((request) {.addr = addr, .func = FUNC_WRITE, .cache_id = cache_id, .trans_id = trans_id});
    } 

    void stats_print() {
        cout << "Memory reads: " << totalreadreq << endl;
        cout << "Memory writes: " << totalwritereq << endl;
        cout << "Total aquisition time: " << totalaqtime << endl;
        cout << "Average aquisition time: " << totalaqtime / (totalreadreq + totalwritereq)  << endl;
    }
};
#endif

