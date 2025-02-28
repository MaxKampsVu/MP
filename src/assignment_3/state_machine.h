#include <iostream>
#include <iomanip>
#include <systemc>
#define SC_ALLOW_DEPRECATED_IEEE_API

#include "helpers.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.

class CacheController




enum State {Invalid, Exclusive, Modified, Owned, Shared};






struct ForgeinAddr {
    uint64_t tag; // Stores the block adress
    uint64_t cache_id;
    State state;
};


std::unordered_map<>


state addr_state(uint64_t addr) {
    return Invalid;
}

void next_state(Function fun, uint64_t addr) {
    switch (addr_state(addr))
    {
    case Invalid:
        break;
    case Exclusive:
        break;
    case Modified:
        break;
    case Owned:
        break;
    case Shared:
        break;
    default:
        break;
    }
}

void invalid_func(Function fun, uint64_t addr) {

}