#ifndef _HELPERS_H_
#define _HELPERS_H_

#include <iomanip>
#include <iostream>
#include <systemc.h>

const int t_width = 7;
const int n_width = 7;

using namespace std;

enum Function {FUNC_READ, FUNC_WRITE, FUNC_NOP};

static bool VERBOSE = true; // Toggle logging  

static size_t NUM_CPUS = 2; 

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

inline void log_rest() {
    cout << endl;
}

template <typename T, typename... Tail>
void log_rest(const char *n1, T v1, Tail... tail) {
    // log head
    cout << ": " << n1 << ": " << v1;
    log_rest(tail...);
}

/* Log a simple message. */
inline void log(const char *comp, const char *msg) {
    if (sc_report_handler::get_verbosity_level() >= SC_MEDIUM) {
        cout << setw(t_width) << sc_time_stamp() << ": " << setw(n_width) << comp;
        cout << ": " << msg << endl;
    }
}

/* Log the state change of a component to std out.
 * First argument is the name of component, followed by pairs
 * of name, values that need to be printed. */
template <typename T, typename... Tail>
void log(const char *comp, const char *n1, T v1, Tail... tail) {
    if (sc_report_handler::get_verbosity_level() >= SC_MEDIUM) {
        // timestamp and name
        cout << setw(t_width) << sc_time_stamp() << ": " << setw(n_width) << comp;
        // log head
        cout << ": " << n1 << ": " << v1;
        // log tail
        log_rest(tail...);
    }
}

#endif
