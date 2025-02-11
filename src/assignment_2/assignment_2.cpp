/*
 * File: assignment1.cpp
 *
 * Framework to implement Task 1 of the Multi-Core Processor Systems lab
 * session. This uses the framework library to interface with tracefiles which
 * will drive the read/write requests
 *
 * Author(s): Michiel W. van Tol, Mike Lankamp, Jony Zhang,
 *            Konstantinos Bousias, Simon Polstra
 *
 */

#include <iostream>
#include <iomanip>
#include <systemc>
#define SC_ALLOW_DEPRECATED_IEEE_API

#include "psa.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.

struct CacheLine {
    uint64_t tag;
    double lu_time;
    bool valid;
};

static const size_t CACHE_SIZE = 32000; // Byte 
static const size_t SET_ASSOC = 8;
static const size_t LINE_SIZE = 32; // Byte 
static const size_t N_SETS = (CACHE_SIZE / LINE_SIZE) / SET_ASSOC; 


SC_MODULE(Cache) {
    public:
    enum Function { FUNC_READ, FUNC_WRITE };

    enum RetCode { RET_READ_DONE, RET_WRITE_DONE };

    enum RetStatusCode { RET_CACHE_MISS, RET_CACHE_HIT };

    sc_in<bool> Port_CLK;
    sc_in<Function> Port_Func;
    sc_in<uint64_t> Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<64> Port_Data;
    sc_out<RetStatusCode> Port_Status;

    SC_CTOR(Cache) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        line_table = (CacheLine **)malloc(sizeof(CacheLine *) * N_SETS);
        for (size_t i = 0; i < N_SETS; i++) {
            line_table[i] = (CacheLine *)malloc(sizeof(CacheLine) * SET_ASSOC);

            for(size_t j = 0; j < SET_ASSOC; j++) {
                line_table[i][j] = (CacheLine) {.tag = 0, .lu_time = 0.0, .valid = false};
            }
        }

        cout << "---------- Cache Specs --------" << endl;
        cout << "Cache size: " << CACHE_SIZE << " B" << endl;
        cout << "Line size: " << LINE_SIZE << " B" << endl;
        cout << "Set associativity: " << SET_ASSOC << endl;
        cout << "Number of Sets: " << N_SETS << endl;
        cout << "-------------------------------" << endl;
    } 

    private:
    CacheLine **line_table;

    uint64_t probe_cache(CacheLine *c_set, uint64_t addr) {
        double min_lu_time = UINT64_MAX;
        double min_lu_index = 0;
        int i = 0;

        while (i < SET_ASSOC) {
            if(!c_set[i].valid) { // Empty cache line 
               min_lu_index = i;
               min_lu_time = 0;
               break;
            }

            if(c_set[i].tag == addr) { // Cache hit 
                wait(1);
                cout << sc_time_stamp() << ": Write Cache hit" << endl;
                Port_Status.write(RET_CACHE_HIT);
                return i;
            }

            if (c_set[i].lu_time < min_lu_time) { // Evict least recently used 
                min_lu_index = i;
                min_lu_time = c_set[i].lu_time;
            }

            i++;
        }

        // Cache miss 
        cout << sc_time_stamp() << ": Write Cache miss, fetching from main" << endl;
        wait(100); // TODO: Ask if this takes a 100 cycles in the case that we write back an element and the case that we write in an empty cache cell 
        Port_Status.write(RET_CACHE_MISS);
        return min_lu_index;
    }

    void write_cache(CacheLine *c_set, uint64_t addr, uint64_t index) {
        if (c_set[index].valid) {
            cout << sc_time_stamp() << ": Cache evicts " << c_set[index].tag << endl;
        }
    
       c_set[index] = (CacheLine) {.tag = addr, .lu_time = sc_time_stamp().to_double(), .valid = true};
    }

    void read_cache(CacheLine *c_set, uint64_t addr) {
        int i = 0;

        while (i < SET_ASSOC) {
            if(c_set[i].tag == addr && c_set[i].valid) { // Cache hit 
                c_set[i].lu_time = sc_time_stamp().to_double(); // Refresh last used time
                wait(1);
                cout << sc_time_stamp() << ": Read Cache hit" << endl;
                cout << sc_time_stamp() << ": Cache refreshes last used time of " << addr << " to " << sc_time_stamp() << endl;
                Port_Status.write(RET_CACHE_HIT);
                return;
            }

            i++;
        }

        cout << sc_time_stamp() << ": Read Cache miss, fetching from main" << endl;
        wait(100); // Cache miss 
        Port_Status.write(RET_CACHE_MISS);
    }

    void write_memory() {

    }

    void execute() {
        while (true) {
            wait(Port_Func.value_changed_event());

            // Receive function from CPU
            Function f = Port_Func.read();
            uint64_t addr = Port_Addr.read();
            uint64_t data = 0;

            // Determine cache set for addr
            size_t set_index = addr % N_SETS;
            CacheLine *c_set = line_table[set_index];

            if (f == FUNC_WRITE) {
                cout << sc_time_stamp() << ": Cache receives " << addr << " write" << endl;
                data = Port_Data.read().to_uint64();
                uint64_t index = probe_cache(c_set, addr);
                write_cache(c_set, addr, index);
                cout << sc_time_stamp() << ": Cache writes " << addr << " to set " << set_index << " in cache line " << index << endl;
            } else {
                cout << sc_time_stamp() << ": Cache receives " << addr << " read" << endl;
                read_cache(c_set, addr);
            }

            if (f == FUNC_READ) {
                Port_Data.write(0); // Data is never stored in the simulated cache, so we can just send 0 
                Port_Done.write(RET_READ_DONE);
                wait();
                Port_Data.write(float_64_bit_wire); // string with 64 "Z"'s
            } else {
                Port_Done.write(RET_WRITE_DONE);
            }
        }
    }
};

SC_MODULE(CPU) {
    public:
    sc_in<bool> Port_CLK;
    sc_in<Cache::RetCode> Port_cacheDone;
    sc_in<Cache::RetStatusCode> Port_cacheStatus;
    sc_out<Cache::Function> Port_cacheFunc;
    sc_out<uint64_t> Port_cacheAddr;
    sc_inout_rv<64> Port_cacheData;

    SC_CTOR(CPU) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
    }

    private:
    void execute() {
        TraceFile::Entry tr_data;
        Cache::Function f;


        // Loop until end of tracefile
        while (!tracefile_ptr->eof()) {
            // Get the next action for the processor in the trace
            if (!tracefile_ptr->next(0, tr_data)) {
                cerr << "Error reading trace for CPU" << endl;
                break;
            }

            switch (tr_data.type) {
                case TraceFile::ENTRY_TYPE_READ:
                    f = Cache::FUNC_READ; 
                    break;
                case TraceFile::ENTRY_TYPE_WRITE:
                    f = Cache::FUNC_WRITE;
                    break;
                case TraceFile::ENTRY_TYPE_NOP: 
                    break;
                default: cerr << "Error, got invalid data from Trace" << endl; exit(0);
            }
            

            if (tr_data.type != TraceFile::ENTRY_TYPE_NOP) {
                Port_cacheAddr.write(tr_data.addr);
                Port_cacheFunc.write(f);

                if (f == Cache::FUNC_WRITE) {
                    cout << sc_time_stamp() << ": CPU sends " << tr_data.addr << " write" << endl;

                    // Don't have data, we write the address as the data value.
                    Port_cacheData.write(tr_data.addr);
                    wait();
                    // Now float the data wires with 64 "Z"'s
                    Port_cacheData.write(float_64_bit_wire);

                } else {
                    cout << sc_time_stamp() << ": CPU sends " << tr_data.addr << " read" << endl;
                }

                wait(Port_cacheDone.value_changed_event());

                if (f == Cache::FUNC_READ) {
                    cout << sc_time_stamp()
                         << ": CPU reads: " << Port_cacheData.read() << endl;
                }
            } else {
                cout << sc_time_stamp() << ": CPU executes NOP" << endl;
            }

            // Log cache hit 
            int j = Port_cacheStatus.read();

            switch (tr_data.type) {
                case TraceFile::ENTRY_TYPE_READ:
                    if (j)
                        stats_readhit(0);
                    else
                        stats_readmiss(0);
                    break;
                case TraceFile::ENTRY_TYPE_WRITE:
                    if (j)
                        stats_writehit(0);
                    else
                        stats_writemiss(0);
                    break;
                default: break;
            }

            // Advance one cycle in simulated time
            wait();
        }

        // Finished the Tracefile, now stop the simulation
        sc_stop();
    }
};



class Bus_if : public virtual sc_interface
{
public:
    virtual bool read(uint64_t addr) = 0;
    virtual bool write(uint64_t addr, uint64_t data) = 0;
};

class Bus : public Bus_if, public sc_module
{
public:
    enum Function { FUNC_READ, FUNC_WRITE };

    sc_in<bool> Port_CLK;
    sc_signal_rv<64> Port_BusAddr;
    sc_in<Function> Port_Func;

private:
    sc_signal_rv<64> *sc_signal_rv_procs;

public:
    SC_CTOR (Bus)
    {
        //sc_signal_rv_procs = (sc_signal_rv<64>*)malloc(sizeof(CacheLine) * );
    }

    virtual bool read(uint64_t addr)
    {
        Port_BusAddr.write(addr);
        return true;
    };

    virtual bool write(uint64_t addr, uint64_t data)
    {
        Port_BusAddr.write(addr);
        return true;
    }
};

int sc_main(int argc, char *argv[]) {
    try {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus
        init_tracefile(&argc, &argv);

        // Initialize statistics counters
        stats_init();

        // Instantiate Modules
        Cache cache("cache");
        CPU cpu("cpu");

        // Signals
        sc_buffer<Cache::Function> sigcacheFunc;
        sc_buffer<Cache::RetCode> sigcacheDone;
        sc_buffer<Cache::RetStatusCode> sigcacheStatus;
        sc_signal<uint64_t> sigcacheAddr;
        sc_signal_rv<64> sigcacheData;

        // The clock that will drive the CPU and cacheory
        sc_clock clk;

        // Connecting module ports with signals
        cache.Port_Func(sigcacheFunc);
        cache.Port_Addr(sigcacheAddr);
        cache.Port_Data(sigcacheData);
        cache.Port_Done(sigcacheDone);
        cache.Port_Status(sigcacheStatus);

        cpu.Port_cacheFunc(sigcacheFunc);
        cpu.Port_cacheAddr(sigcacheAddr);
        cpu.Port_cacheData(sigcacheData);
        cpu.Port_cacheDone(sigcacheDone);
        cpu.Port_cacheStatus(sigcacheStatus);

        cache.Port_CLK(clk);
        cpu.Port_CLK(clk);

        cout << "Running (press CTRL+C to interrupt)... " << endl;


        // Start Simulation
        sc_start();

        // Print statistics after simulation finished
        stats_print();
        // cache.dump(); // Uncomment to dump cacheory to stdout.
      
    }
    catch (exception &e) {
        cerr << e.what() << endl;
    }

    return 0;
}
