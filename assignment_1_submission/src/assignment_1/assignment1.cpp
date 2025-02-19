/*
 * File: assignment1.cpp
 */

#include <iostream>
#include <iomanip>
#include <systemc>
#define SC_ALLOW_DEPRECATED_IEEE_API

#include "psa.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.

struct CacheLine {
    uint64_t tag; // Stores the block adress
    double lu_time; // least recently used time for cache eviction 
    bool valid;
    bool dirty; 
};

static const size_t CACHE_SIZE = 32768; // Byte 
static const size_t SET_ASSOC = 8;
static const size_t LINE_SIZE = 32; // Byte 
static const size_t N_SETS = (CACHE_SIZE / LINE_SIZE) / SET_ASSOC; 
static bool VERBOSE = true; // Toggle logging  

SC_MODULE(Cache) {
    public:
    enum Function { FUNC_READ, FUNC_WRITE };

    enum RetCode { RET_READ_DONE, RET_WRITE_DONE };

    enum RetStatusCode { RET_CACHE_MISS, RET_CACHE_HIT }; // Status code to signify to the cpu if the read/write cause a hit/miss 

    sc_in<bool> Port_CLK;
    sc_in<Function> Port_Func;
    sc_in<uint64_t> Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<64> Port_Data;
    sc_out<RetStatusCode> Port_Status; // Wire for the hit/miss status code 

    SC_CTOR(Cache) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        line_table = (CacheLine **)malloc(sizeof(CacheLine *) * N_SETS); // Initialite the cache line 
        for (size_t i = 0; i < N_SETS; i++) {
            line_table[i] = (CacheLine *)malloc(sizeof(CacheLine) * SET_ASSOC);

            for(size_t j = 0; j < SET_ASSOC; j++) {
                line_table[i][j] = (CacheLine) {.tag = 0, .lu_time = 0.0, .valid = false, .dirty = false};
            }
        }

        VERBOSE && cout << "---------- Cache Specs --------" << endl;
        VERBOSE && cout << "Cache size: " << CACHE_SIZE << " B" << endl;
        VERBOSE && cout << "Line size: " << LINE_SIZE << " B" << endl;
        VERBOSE && cout << "Set associativity: " << SET_ASSOC << endl;
        VERBOSE && cout << "Number of Sets: " << N_SETS << endl;
        VERBOSE && cout << "-------------------------------" << endl;
    } 

    void dump() {
        for (size_t i = 0; i < N_SETS; i++) {
            cout << "Cache set: " << i << endl;    
            for(size_t j = 0; j < SET_ASSOC; j++) {
                cout << "   Cache Line: " << j << " {tag=" << line_table[i][j].tag << ", lu_time=" << line_table[i][j].lu_time << ", valid=" << line_table[i][j].valid << ", dirty=" << line_table[i][j].dirty << "}" << endl;    
            }
        }
    }

    private:
    CacheLine **line_table; // Array of Sets with size SET_ASSOC (e.g. 8 for an 8-way set-associative cache)

    // Returns the index in a cache set into which a new address should be inserted to and informs the cpu about a hit/miss
    uint64_t probe_cache(CacheLine *c_set, uint64_t block_addr) {
        double min_lu_time = UINT64_MAX;
        double min_lu_index = 0;
        int i = 0;

        while (i < SET_ASSOC) {
            if(!c_set[i].valid) { // Find an empty cache line for insertion
               min_lu_index = i;
               min_lu_time = 0;
            }
            else if(c_set[i].tag == block_addr) { // If a cache hit is found return immidiately  
                VERBOSE && cout << sc_time_stamp() << ": Cache hit" << endl;
                Port_Status.write(RET_CACHE_HIT);
                return i;
            }

            else if (c_set[i].lu_time < min_lu_time) { // Find a cache line to evict for insertion
                min_lu_index = i;
                min_lu_time = c_set[i].lu_time;
            }

            i++;
        }

        // Cache miss 
        VERBOSE && cout << sc_time_stamp() << ": Cache miss, fetching from main" << endl;
        Port_Status.write(RET_CACHE_MISS);
        return min_lu_index;
    }

    // Refreshes the last recently used time
    void refresh_lu_time(CacheLine *c_set, uint64_t block_addr, uint64_t index) {
        c_set[index].lu_time = sc_time_stamp().to_double(); // Refresh last used time
        VERBOSE && cout << sc_time_stamp() << ": Cache refreshes last used time of " << block_addr << " to " << sc_time_stamp() << endl;
    }


    // Inserts a CacheLine into a set and evicts a coliding cache line if necessary
    void allocate(CacheLine *c_set, uint64_t block_addr, uint64_t index, bool is_write) {
        if(c_set[index].tag != block_addr && c_set[index].valid) { // evict element
            VERBOSE && cout << sc_time_stamp() << ": Cache evicts " << c_set[index].tag << endl;
            if(c_set[index].dirty) { // writeback if dirty
                wait(100); 
                VERBOSE && cout << sc_time_stamp() << ": Cache write back dirty block_addr " << c_set[index].tag << " to main" << endl;
            }
        }
        wait(100); 
        c_set[index] = (CacheLine) {.tag = block_addr, .lu_time = sc_time_stamp().to_double(), .valid = true, .dirty = is_write};
        VERBOSE && cout << sc_time_stamp() << ": Cache writes " << c_set[index].tag << endl;
    }

    void write_cache(CacheLine *c_set, uint64_t block_addr, uint64_t index) {
        if (c_set[index].tag == block_addr && c_set[index].valid) { // Cache hit 
            refresh_lu_time(c_set, block_addr, index);
            c_set[index].dirty = true;
            wait(1);
        } else { // Load block_addr from main memory and evict if necessary 
            allocate(c_set, block_addr, index, true);
        }
    }

    void read_cache(CacheLine *c_set, uint64_t block_addr, uint64_t index) {
        if (c_set[index].tag == block_addr && c_set[index].valid) { // Cache hit 
            refresh_lu_time(c_set, block_addr, index);
            wait(1);
        } else { // Load block_addr from main memory and evict if necessary 
            allocate(c_set, block_addr, index, false);
        }
    }

    void execute() {
        while (true) {
            wait(Port_Func.value_changed_event());

            // Receive function from CPU
            Function f = Port_Func.read();
            uint64_t addr = Port_Addr.read();
            uint64_t block_addr = addr / LINE_SIZE;
            uint64_t data = 0;

            // Determine cache set for block_addr
            size_t set_index = block_addr % N_SETS;
            CacheLine *c_set = line_table[set_index];

            // Find the index in the c_set at which cache line should be manipulated (in case of a hit) / inserted (in case of a miss)
            uint64_t index = probe_cache(c_set, block_addr); 
            if (f == FUNC_WRITE) {
                data = Port_Data.read().to_uint64();
                write_cache(c_set, block_addr, index);
            } else {
                read_cache(c_set, block_addr, index);
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
    sc_out<Function> Port_cacheFunc;
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
        Function f;


        // Loop until end of tracefile
        while (!tracefile_ptr->eof()) {
            // Get the next action for the processor in the trace
            if (!tracefile_ptr->next(0, tr_data)) {
                cerr << "Error reading trace for CPU" << endl;
                break;
            }

            switch (tr_data.type) {
                case TraceFile::ENTRY_TYPE_READ:
                    f = FUNC_READ; 
                    break;
                case TraceFile::ENTRY_TYPE_WRITE:
                    f = FUNC_WRITE;
                    break;
                case TraceFile::ENTRY_TYPE_NOP: 
                    break;
                default: cerr << "Error, got invalid data from Trace" << endl; exit(0);
            }
            

            if (tr_data.type != TraceFile::ENTRY_TYPE_NOP) {
                Port_cacheAddr.write(tr_data.addr);
                Port_cacheFunc.write(f);

                if (f == FUNC_WRITE) {
                    VERBOSE && cout << sc_time_stamp() << ": CPU sends " << tr_data.addr << " write" << endl;
                    // Don't have data, we write the address as the data value.
                    Port_cacheData.write(tr_data.addr);
                    wait();
                    // Now float the data wires with 64 "Z"'s
                    Port_cacheData.write(float_64_bit_wire);

                } else {
                    VERBOSE && cout << sc_time_stamp() << ": CPU sends " << tr_data.addr << " read" << endl;
                }

                wait(Port_cacheDone.value_changed_event());

                if (f == FUNC_READ) {
                    VERBOSE && cout << sc_time_stamp()
                         << ": CPU reads: " << Port_cacheData.read() << endl;
                }
            } else {
                VERBOSE && cout << sc_time_stamp() << ": CPU executes NOP" << endl;
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


int sc_main(int argc, char *argv[]) {
    try {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus


        if (argc == 3) {   
            VERBOSE = std::stoi(argv[2]) != 0;
        } else if (argc != 2) {
            throw std::invalid_argument("Usage: ./assignment_1.bin [trace_file] [verbose (0 or 1)] or \n ./assignment_1.bin [trace_file]");
        }

    
        init_tracefile(&argc, &argv);


        // Initialize statistics counters
        stats_init();

        // Instantiate Modules
        Cache cache("cache");
        CPU cpu("cpu");

        // Signals
        sc_buffer<Function> sigcacheFunc;
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
        //cache.dump(); // Uncomment to dump cacheory to stdout.
      
    }
    catch (exception &e) {
        cerr << e.what() << endl;
    }

    return 0;
}
