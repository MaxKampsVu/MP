/*
 * File: assignment1.cpp
 */

 #include <iostream>
 #include <iomanip>
 #include <systemc>
 #define SC_ALLOW_DEPRECATED_IEEE_API
 
 #include "psa.h"
 #include "Memory.h"
 #include "helpers.h"
 
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

static int bus_lock = 0; // A lock that gives exclusive access to the bus 
static uint64_t trans_id = 1; // Unique ID for each bus request 

SC_MODULE(Cache) {
    public:
    enum RetCode {RET_READ_DONE, RET_WRITE_DONE, RET_NOP_DONE};

    uint64_t my_id;

    Memory *memory;

    sc_in<bool> Port_CLK;

    double num_requests_before_me = 0;

    //Ports to the CPU  
    sc_in<Function> Port_Func;
    sc_in<uint64_t> Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<64> Port_Data;

    //Ports to Bus
    sc_in<Function> Port_BusFunc;
    sc_in<uint64_t> Port_BusAddr;
    sc_in<uint64_t> Port_BusTransId;
    sc_in<uint64_t> Port_BusCacheId;

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
    uint64_t prev_trans_id = 0;

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
                return i;
            }

            else if (c_set[i].lu_time < min_lu_time) { // Find a cache line to evict for insertion
                min_lu_index = i;
                min_lu_time = c_set[i].lu_time;
            }

            i++;
        }

        // Cache miss 
        return min_lu_index;
    }

    // Refreshes the last recently used time
    void refresh_lu_time(CacheLine *c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
        c_set[index].lu_time = sc_time_stamp().to_double(); // Refresh last used time
        VERBOSE ? log(name(), "refresh last used time of addr", addr) : (void)0;
    }


    // Inserts a CacheLine into a set and evicts a colliding cache line if necessary
    void allocate(CacheLine *c_set, uint64_t block_addr, uint64_t addr, uint64_t index, bool is_write) {
        if(c_set[index].tag != block_addr && c_set[index].valid) { // evict element
            VERBOSE ? log(name(), "evict block_addr: ", c_set[index].tag) : (void)0;
        }
        c_set[index] = (CacheLine) {.tag = block_addr, .lu_time = sc_time_stamp().to_double(), .valid = true, .dirty = is_write};
    }

    // Invalidate an address after snooping 
    void invalidate() {
        uint64_t addr_bus = Port_BusAddr.read();
        Function func_bus = Port_BusFunc.read();
        
        VERBOSE ? log(name(), "Snooped bus addr", addr_bus) : (void)0;

        uint64_t block_addr = addr_bus / LINE_SIZE;

        // Determine cache set for block_addr
        size_t set_index = block_addr % N_SETS;
        CacheLine *c_set = line_table[set_index];

        // Find the index in the c_set at which cache line should be manipulated (in case of a hit) / inserted (in case of a miss)
        uint64_t index = probe_cache(c_set, block_addr); 

        //Invalidate block if it is present in cache 
        if(c_set[index].tag == block_addr && c_set[index].valid && func_bus == FUNC_WRITE) {
            c_set[index].valid = false;
            VERBOSE ? log(name(), "Invalidated addr", addr_bus) : (void)0;
            memory->totalinv += 1;
        }
    }

    // Snoop a new memory reply from the bus and invalidate accordingly 
    void wait_and_invalidate() {
        wait(Port_BusTransId.value_changed_event());
        uint64_t trans_id = Port_BusTransId.read();
        uint64_t cache_id = Port_BusCacheId.read();

        if(prev_trans_id != trans_id && cache_id != my_id) {
            num_requests_before_me++;
            prev_trans_id = trans_id;
            invalidate();
        }
    }

    void mem_read(uint64_t addr) {
        memory->read(addr, trans_id, my_id);
        trans_id += 1;
    }
    
    void mem_write(uint64_t addr) {
        memory->write(addr, trans_id, my_id);
        trans_id += 1;
    }

    void nop_cache() {
        while (bus_lock != my_id) {
            wait_and_invalidate();
        }

        VERBOSE && cout << "--------- Cache id " << my_id << " acquired the bus ---------" << endl;
        VERBOSE ? log(name(), "NOP, do nothing") : (void)0;
        bus_lock = (bus_lock + 1) % num_cpus; // Release the lock 

        while(bus_lock != 0) {
            wait_and_invalidate();
        }
    }

    void write_cache(CacheLine *c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
        while (bus_lock != my_id) {
            wait_and_invalidate();
        }
        memory->totalacqtime += sc_time(num_requests_before_me, SC_NS); 
        memory->totalacq += 1;

        VERBOSE && cout << "--------- Cache id " << my_id << " acquired the bus ---------" << endl;
        if (c_set[index].tag == block_addr && c_set[index].valid) { // Cache hit 
            VERBOSE ? log(name(), "Cache write hit") : (void)0;
            stats_writehit(my_id);
            wait(1); // a local cache access takes 1 cycle 
            refresh_lu_time(c_set, block_addr, addr, index);
        } else {
            wait(1); // It takes 1 cycle to write on the bus 
            VERBOSE ? log(name(), "Cache miss, request read from bus for addr", addr) : (void)0;
            stats_writemiss(my_id);
            mem_read(addr);
            wait(Port_BusTransId.value_changed_event());
            wait(100); // It takes 100 for a bus reqeust to be served
            VERBOSE ? log(name(), "reads on bus addr", addr) : (void)0;
            allocate(c_set, block_addr, addr, index, true);
        }
        
        wait(1); // It takes 1 cycle to write on the bus 
        VERBOSE ? log(name(), "finished write to cache", addr) : (void)0;
        VERBOSE ? log(name(), "request bus to write back to memory", addr) : (void)0;
        wait(100); // It takes 100 for a bus reqeust to be served
        mem_write(addr);
        wait(Port_BusTransId.value_changed_event());
        VERBOSE ? log(name(), "finished write back to memory of addr", addr) : (void)0;

        bus_lock = (bus_lock + 1) % num_cpus; // Release the lock 

        num_requests_before_me = 0;

        while(bus_lock != 0) {
            wait_and_invalidate();
        }
    }

    void read_cache(CacheLine *c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
        num_requests_before_me = 0;
        while (bus_lock != my_id) {
            wait_and_invalidate();
        }

        VERBOSE && cout << "--------- Cache id " << my_id << " acquired the bus ---------" << endl;
        if (c_set[index].tag == block_addr && c_set[index].valid) { // Cache hit 
            VERBOSE ? log(name(), "Cache read hit") : (void)0;
            stats_readhit(my_id);
            wait(1); // A local cache access takes 1 cycle 
            refresh_lu_time(c_set, block_addr, addr, index);
        } else { // Load block_addr from main memory and evict if necessary 
            wait(1); // It takes 1 cycle to write on the bus
            VERBOSE ? log(name(), "Cache miss, request read from bus for addr", addr) : (void)0;
            stats_readmiss(my_id);
            memory->totalacq += 1;
            memory->totalacqtime += sc_time(num_requests_before_me, SC_NS);
            mem_read(addr);
            wait(Port_BusTransId.value_changed_event());
            wait(100); // It takes 100 for a bus reqeust to be served
            VERBOSE ? log(name(), "reads on bus addr", addr) : (void)0;
            allocate(c_set, block_addr, addr, index, false);
        }

        bus_lock = (bus_lock + 1) % num_cpus; // Release the lock 

        while(bus_lock != 0) {
            wait_and_invalidate();
        }
    }

    void execute() {
        trans_id = my_id + 1;
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
                write_cache(c_set, block_addr, addr, index);
            } else if (f == FUNC_READ) {
                read_cache(c_set, block_addr, addr, index);
            } else {
                nop_cache();
            }

            if (f == FUNC_READ) {
                Port_Data.write(0); // Data is never stored in the simulated cache, so we can just send 0 
                Port_Done.write(RET_READ_DONE);
                wait();
                Port_Data.write(float_64_bit_wire); // string with 64 "Z"'s
            } else if (f == FUNC_READ) {
                Port_Done.write(RET_WRITE_DONE);
            } else {
                Port_Done.write(RET_NOP_DONE);
            }
        }
    }
};



SC_MODULE(CPU) {
    public:
    sc_in<bool> Port_CLK;
    sc_in<Cache::RetCode> Port_cacheDone;
    sc_out<Function> Port_cacheFunc;
    sc_out<uint64_t> Port_cacheAddr;
    sc_inout_rv<64> Port_cacheData;

    int my_id;

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
            if (!tracefile_ptr->next(my_id, tr_data)) {
                cerr << "Error reading trace for CPU" << endl;
                break;
            }

            if(tracefile_ptr->eof()) {
                sc_stop();
            }
            
            switch (tr_data.type) {
                case TraceFile::ENTRY_TYPE_READ:
                    f = FUNC_READ; 
                    break;
                case TraceFile::ENTRY_TYPE_WRITE:
                    f = FUNC_WRITE;
                    break;
                case TraceFile::ENTRY_TYPE_NOP:
                    f = FUNC_NOP;
                    break;
                default: cerr << "Error, got invalid data from Trace" << endl; exit(0);
            }

            Port_cacheAddr.write(tr_data.addr);
            Port_cacheFunc.write(f);

            if (f == FUNC_WRITE) {
                VERBOSE ? log(name(), "(*) sends write for addr", tr_data.addr) : (void)0;
                // Don't have data, we write the address as the data value.
                Port_cacheData.write(tr_data.addr);
                wait();
                // Now float the data wires with 64 "Z"'s
                Port_cacheData.write(float_64_bit_wire);

            } else if (f == FUNC_READ) {
                VERBOSE ? log(name(), "(*) sends read for addr", tr_data.addr) : (void)0;
            } else {
                VERBOSE ? log(name(), "(*) CPU executes NOP") : (void)0;
                Port_cacheFunc.write(f);
            }
            wait(Port_cacheDone.value_changed_event());

            wait();
        }
        
        // Finished the Tracefile, now stop the simulation
        sc_stop();
    }
};

int sc_main(int argc, char *argv[]) {
    try {
        if (argc == 3) {   
            VERBOSE = std::stoi(argv[2]) != 0;
        } else if (argc != 2) {
            throw std::invalid_argument("Usage: ./assignment_1.bin [trace_file] [verbose (0 or 1)] or \n ./assignment_1.bin [trace_file]");
        }
    
        init_tracefile(&argc, &argv);

        NUM_CPUS = tracefile_ptr->get_proc_count();
        cout << "Executing with " << NUM_CPUS << "CPUS" << endl;

        // Initialize statistics counters
        stats_init();

        cout << "Running (press CTRL+C to interrupt)... " << endl;
        
        // Declare signals for all CPUs and caches
        std::vector<sc_buffer<Function>*> sigcacheFunc(NUM_CPUS);
        std::vector<sc_buffer<Cache::RetCode>*> sigcacheDone(NUM_CPUS);
        std::vector<sc_signal<uint64_t>*> sigcacheAddr(NUM_CPUS);
        std::vector<sc_signal_rv<64>*> sigcacheData(NUM_CPUS);

        // Declare vectors to store pointers to caches and CPUs
        std::vector<Cache*> caches(NUM_CPUS);
        std::vector<CPU*> cpus(NUM_CPUS);

        // Clock signal shared by all modules
        sc_clock clk("clk", sc_time(1, SC_NS));

        Memory *memory = new Memory("memory");
        memory->Port_CLK(clk);

        //Signals between memory and cache
        std::vector<sc_buffer<Function>*> sigbusFunc(NUM_CPUS);
        std::vector<sc_signal<uint64_t>*> sigbusAddr(NUM_CPUS);
        std::vector<sc_buffer<uint64_t>*> sigbusTransId(NUM_CPUS);
        std::vector<sc_buffer<uint64_t>*> sigbusCacheId(NUM_CPUS);
        

        // Initialize Cache and CPU modules, and connect them
        for (int i = 0; i < NUM_CPUS; i++) {
            std::string cache_name = "cache_" + std::to_string(i);
            std::string cpu_name = "cpu_" + std::to_string(i);

            // Allocate Cache and CPU
            caches[i] = new Cache(cache_name.c_str());
            caches[i]->memory = memory;

            cpus[i] = new CPU(cpu_name.c_str());

            cpus[i]->my_id = i;
            caches[i]->my_id = i;

            // Allocate signals
            sigcacheFunc[i] = new sc_buffer<Function>();
            sigcacheDone[i] = new sc_buffer<Cache::RetCode>();
            sigcacheAddr[i] = new sc_signal<uint64_t>();
            sigcacheData[i] = new sc_signal_rv<64>();
            sigbusFunc[i] = new sc_buffer<Function>();
            sigbusAddr[i] = new sc_signal<uint64_t>();
            sigbusTransId[i] = new sc_buffer<uint64_t>();
            sigbusCacheId[i] = new sc_buffer<uint64_t>();

            // Connecting ports of Cache and CPU with the corresponding signals
            caches[i]->Port_Func(*sigcacheFunc[i]);
            caches[i]->Port_Addr(*sigcacheAddr[i]);
            caches[i]->Port_Data(*sigcacheData[i]);
            caches[i]->Port_Done(*sigcacheDone[i]);

            caches[i]->Port_BusFunc(*sigbusFunc[i]);
            caches[i]->Port_BusAddr(*sigbusAddr[i]);
            caches[i]->Port_BusTransId(*sigbusTransId[i]);
            caches[i]->Port_BusCacheId(*sigbusCacheId[i]);

            (*memory->Port_BusCacheFunc[i])(*sigbusFunc[i]);
            (*memory->Port_BusCacheAddr[i])(*sigbusAddr[i]);
            (*memory->Port_BusCacheTransId[i])(*sigbusTransId[i]);
            (*memory->Port_BusCacheCacheId[i])(*sigbusCacheId[i]);

            cpus[i]->Port_cacheFunc(*sigcacheFunc[i]);
            cpus[i]->Port_cacheAddr(*sigcacheAddr[i]);
            cpus[i]->Port_cacheData(*sigcacheData[i]);
            cpus[i]->Port_cacheDone(*sigcacheDone[i]);

            // Connect clock signal to both Cache and CPU
            caches[i]->Port_CLK(clk);
            cpus[i]->Port_CLK(clk);
        }

        // Start Simulation
        sc_start();

        // Print statistics after simulation finished
        stats_print();

        // Print bus statistics 
        memory->stats_print();
    }
    catch (exception &e) {
        cerr << e.what() << endl;
    }

    return 0;
}
