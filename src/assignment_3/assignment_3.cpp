#include <iostream>
#include <iomanip>
#include <systemc>
 #include "psa.h"
#define SC_ALLOW_DEPRECATED_IEEE_API

#include "helpers.h"
#include "Cache.h"
#include "CacheController.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.


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
        cout << "Executing with " << NUM_CPUS << " CPUS" << endl;

        cout << "Starting controller simulation...\n";
        // Create a CacheController instance with a maximum size of 10 (arbitrary)

        // Declare signals for all CPUs and caches
        std::vector<sc_buffer<Function>*> sigcacheFunc(NUM_CPUS);
        std::vector<sc_buffer<Cache::RetCode>*> sigcacheDone(NUM_CPUS);
        std::vector<sc_signal<uint64_t>*> sigcacheAddr(NUM_CPUS);
        std::vector<sc_signal_rv<64>*> sigcacheData(NUM_CPUS);

        // Declare signals for the CC
        sc_signal<uint64_t, SC_MANY_WRITERS> sigtransIdCC;
        sc_signal<uint64_t, SC_MANY_WRITERS> sigcacheIdCC;

        // Clock signal shared by all modules
        sc_clock clk("clk", sc_time(1, SC_NS));

        std::vector<Cache*> caches(NUM_CPUS);
        std::vector<CPU*> cpus(NUM_CPUS);


        caches.resize(NUM_CPUS);
        CacheController cacheController("CC");
        cacheController.Port_CacheTransId(sigtransIdCC);
        cacheController.Port_CacheCacheId(sigcacheIdCC);

        for(int i = 0; i < NUM_CPUS; i++) {
            // Allocate signals
            sigcacheFunc[i] = new sc_buffer<Function>();
            sigcacheDone[i] = new sc_buffer<Cache::RetCode>();
            sigcacheAddr[i] = new sc_signal<uint64_t>();
            sigcacheData[i] = new sc_signal_rv<64>();

            //Init cache 
            std::string cache_name = "cache_" + std::to_string(i);
            caches[i] = new Cache(cache_name.c_str());
            caches[i]->my_id = i;
            caches[i]->Port_CCTransId(sigtransIdCC);
            caches[i]->Port_CCCacheId(sigcacheIdCC);
            caches[i]->cacheController = &cacheController;
            caches[i]->Port_Func(*sigcacheFunc[i]);
            caches[i]->Port_Addr(*sigcacheAddr[i]);
            caches[i]->Port_Data(*sigcacheData[i]);
            caches[i]->Port_Done(*sigcacheDone[i]);
            caches[i]->Port_CLK(clk);

            //Init cpu 
            std::string cpu_name = "cpu_" + std::to_string(i);
            cpus[i] = new CPU(cpu_name.c_str());
            cpus[i]->my_id = i;
            cpus[i]->Port_cacheFunc(*sigcacheFunc[i]);
            cpus[i]->Port_cacheAddr(*sigcacheAddr[i]);
            cpus[i]->Port_cacheData(*sigcacheData[i]);
            cpus[i]->Port_cacheDone(*sigcacheDone[i]);
            cpus[i]->Port_CLK(clk);

            //Connect cc and caches 
            cacheController.caches[i] = caches[i];
        }

        sc_start();

        stats_print();

        cout << "controller simulation complete.\n";

    } catch (exception &e) {
        cerr << e.what() << endl;
    }

    return 0;
}




/*
        cout << "Completed initialization " << endl;

        uint64_t addrA = 0xABC, addrB = 0xDEF;

        int test = 2;

        if(test == 0) {
            // Cache 0 becomes exclusive owner 
            cacheController.update(addrA, 0, FUNC_READ, false, 1);
            // Cache 0 is still exclusive owner 
            cacheController.update(addrA, 0, FUNC_READ, true, 2);
            // Address becomes shared between 0 and 1 
            cacheController.update(addrA, 1, FUNC_READ, false, 3);
            // Adress is still shared 
            cacheController.update(addrA, 1, FUNC_READ, true, 4);
            // Address is modified by 0 and invalidated by 1 
            cacheController.update(addrA, 0, FUNC_WRITE, true, 5);
        }

        if (test == 1) {
            // Cache 0 becomes modified
            cacheController.update(addrA, 0, FUNC_WRITE, false, 1);
            // Cache 0 becomes owner / Cache 1 becomes shared 
            cacheController.update(addrA, 1, FUNC_READ, false, 2);
            // Cache 0 becomes modified / Cache 1 becomes invalid 
            cacheController.update(addrA, 0, FUNC_WRITE, true, 3);
        } 

        if (test == 2) {
            caches[0]->write_cache(0, 1, 2, 3);
        }


*/