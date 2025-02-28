#ifndef CACHE_H
#define CACHE_H

#include <iostream>
#include <iomanip>
#include <systemc>
#include "psa.h"
#include "helpers.h"

#define SC_ALLOW_DEPRECATED_IEEE_API

using namespace std;
using namespace sc_core; // Only import what is needed to avoid polluting the namespace

class CacheController;

static int bus_lock = 0;  // A lock that gives exclusive access to the bus
static uint64_t trans_id_ctr = 1; // Unique ID for each bus request

SC_MODULE(Cache) {
public:
    enum RetCode { RET_READ_DONE, RET_WRITE_DONE, RET_NOP_DONE };

    uint64_t my_id;

    CacheController* cacheController;

    sc_in<bool> Port_CLK;
    double num_requests_before_me = 0;

    // Ports to the CPU  
    sc_in<Function> Port_Func;
    sc_in<uint64_t> Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<64> Port_Data;

    // Ports to the Cache Controller 
    sc_in<uint64_t> Port_CCTransId;
    sc_in<uint64_t> Port_CCCacheId;

    SC_CTOR(Cache);
    void dump();

    void invalidate(uint64_t addr);
    void write_cache(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index);
private:
    CacheLine** line_table; // Cache structure (Set-Associative Cache)
    uint64_t prev_trans_id = 0;

    // Private helper functions
    uint64_t probe_cache(CacheLine* c_set, uint64_t block_addr);
    bool is_cache_hit(CacheLine* c_set, uint64_t block_addr);
    void allocate(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index, bool is_write);
    void wait_and_invalidate();
    void insert(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index);
    void refresh_lu_time(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index);
    void set_dirty(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index);
    void nop_cache();
    void read_cache(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index);
    void execute();
};

#endif // CACHE_H
