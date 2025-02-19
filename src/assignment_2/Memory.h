#ifndef MEMORY_H
#define MEMORY_H
#define SC_ALLOW_DEPRECATED_IEEE_API

#include <iostream>
#include <systemc.h>

#include "bus_slave_if.h"
#include "helpers.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you nee

static size_t NUM_CACHES;

class Memory : public bus_slave_if, public sc_module {
    public:

    sc_in<bool> Port_CLK;
    // Connections to caches
    std::vector<sc_out<Function>*> Port_BusCacheFunc;
    std::vector<sc_out<uint64_t>*> Port_BusCacheAddr;
    std::vector<sc_out<uint64_t>*> Port_BusCacheTransId;


    SC_CTOR(Memory) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        init_ports(2);
    }

    void init_ports(uint64_t NUM_CPUS) {
        NUM_CACHES = NUM_CPUS;
        Port_BusCacheFunc.resize(NUM_CACHES);
        Port_BusCacheAddr.resize(NUM_CACHES);
        Port_BusCacheTransId.resize(NUM_CACHES);

        for (int i = 0; i < NUM_CACHES; i++) {
            Port_BusCacheFunc[i] = new sc_out<Function>();  // Allocate memory
            Port_BusCacheAddr[i] = new sc_out<uint64_t>();
            Port_BusCacheTransId[i] = new sc_out<uint64_t>();
        }
    }

    ~Memory() {
        // nothing to do here right now.
    }

    void execute() {

    }

    int read(uint64_t addr, uint64_t trans_id) {
        assert((addr & 0x3) == 0);
        log(name(), "Transaction id: ", trans_id);
        log(name(), "read from address", addr);
        wait(100);
        return 0;
    }

    int write(uint64_t addr, uint64_t trans_id) {
        assert((addr & 0x3) == 0);
        log(name(), "Transaction id: ", trans_id);
        log(name(), "write to address", addr);
        wait(100);
        return 0;
    }    
};
#endif
