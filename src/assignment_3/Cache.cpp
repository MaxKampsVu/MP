#include "Cache.h"
#include "CacheController.h"

// Static variables

Cache::Cache(sc_module_name name) : sc_module(name) {
    SC_THREAD(execute); 
    sensitive << Port_CLK.pos();
    dont_initialize();

    line_table = (CacheLine**)malloc(sizeof(CacheLine*) * N_SETS);
    for (size_t i = 0; i < N_SETS; i++) {
        line_table[i] = (CacheLine*)malloc(sizeof(CacheLine) * SET_ASSOC);
        for (size_t j = 0; j < SET_ASSOC; j++) {
            line_table[i][j] = {0, 0.0, false, false};
        }
    }

    VERBOSE && cout << "---------- Cache Specs --------" << endl;
    VERBOSE && cout << "Cache size: " << CACHE_SIZE << " B" << endl;
    VERBOSE && cout << "Line size: " << LINE_SIZE << " B" << endl;
    VERBOSE && cout << "Set associativity: " << SET_ASSOC << endl;
    VERBOSE && cout << "Number of Sets: " << N_SETS << endl;
    VERBOSE && cout << "-------------------------------" << endl;
}

void Cache::dump() {
    for (size_t i = 0; i < N_SETS; i++) {
        cout << "Cache set: " << i << endl;
        for (size_t j = 0; j < SET_ASSOC; j++) {
            cout << "   Cache Line: " << j << " {tag=" << line_table[i][j].tag 
                 << ", lu_time=" << line_table[i][j].lu_time 
                 << ", valid=" << line_table[i][j].valid 
                 << ", dirty=" << line_table[i][j].dirty << "}" << endl;
        }
    }
}

uint64_t Cache::probe_cache(CacheLine* c_set, uint64_t block_addr) {
    double min_lu_time = UINT64_MAX;
    uint64_t min_lu_index = 0;

    for (size_t i = 0; i < SET_ASSOC; i++) {
        if (!c_set[i].valid) { // Find an empty cache line
            return i;
        }
        if (c_set[i].tag == block_addr) { // Cache hit
            return i;
        }
        if (c_set[i].lu_time < min_lu_time) { // Find least recently used
            min_lu_index = i;
            min_lu_time = c_set[i].lu_time;
        }
    }
    return min_lu_index; // Cache miss
}

bool Cache::is_cache_hit(CacheLine* c_set, uint64_t block_addr) {
    uint64_t index = probe_cache(c_set, block_addr);
    if((c_set[index].tag == block_addr && c_set[index].valid)) {
        VERBOSE ? log(name(), "Cache hit") : (void)0;
        return true;
    } else {
        VERBOSE ? log(name(), "Cache miss") : (void)0;
        return false;
    }
}


void Cache::allocate(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index, bool is_write) {
    if (c_set[index].tag != block_addr && c_set[index].valid) { // Evict element
        VERBOSE ? log(name(), "evict block_addr: ", c_set[index].tag) : (void)0;
    }
    c_set[index] = {block_addr, sc_time_stamp().to_double(), true, is_write};
}


void Cache::refresh_lu_time(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
    c_set[index].lu_time = sc_time_stamp().to_double();
    VERBOSE ? log(name(), "refresh last used time of addr", addr) : (void)0;
}

void Cache::insert(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
    c_set[index] = {block_addr, sc_time_stamp().to_double(), true, false};
    VERBOSE ? log(name(), "inserted address", addr) : (void)0;
}

void Cache::invalidate(uint64_t addr) {
    uint64_t block_addr = addr / LINE_SIZE;
    size_t set_index = block_addr % N_SETS;
    CacheLine* c_set = line_table[set_index];
    uint64_t index = probe_cache(c_set, block_addr);

    c_set[index].valid = false;
    VERBOSE ? log(name(), "invalidated address", addr) : (void)0;
}

void Cache::set_dirty(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
    c_set[index].dirty = true;
    VERBOSE ? log(name(), "set dirty address", addr) : (void)0;
}



void Cache::wait_and_invalidate() {
    wait(Port_CCTransId.value_changed_event());
    uint64_t trans_id = Port_CCTransId.read();
    uint64_t cache_id = Port_CCCacheId.read(); 
    if (prev_trans_id != trans_id && cache_id != my_id) {
        num_requests_before_me++;
        prev_trans_id = trans_id;
    }
    wait();
}

void Cache::nop_cache() {
    while (bus_lock != my_id) {
        wait_and_invalidate();
    }

    VERBOSE && cout << "--------- Cache id " << my_id << " acquired the bus ---------" << endl;
    VERBOSE ? log(name(), "NOP, do nothing") : (void)0;
    bus_lock = (bus_lock + 1) % num_cpus; // Release the lock

    while (bus_lock != 0) {
        wait_and_invalidate();
    }
}

void Cache::write_cache(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
    while (bus_lock != my_id) {
        wait_and_invalidate();
    }

    VERBOSE && cout << "--------- Cache id " << my_id << " acquired the bus ---------" << endl;

    bool cache_hit = is_cache_hit(c_set, block_addr);
    cacheController->update(addr, my_id, FUNC_WRITE, cache_hit, trans_id_ctr);

    wait(Port_CCTransId.value_changed_event());

    if(!cache_hit) {
        wait(100);
        insert(c_set, block_addr, addr, index);
    } 
    set_dirty(c_set, block_addr, addr, index);

    trans_id_ctr++;
    bus_lock = (bus_lock + 1) % num_cpus; //  Release the lock
    num_requests_before_me = 0;


    while (bus_lock != 0) {
        wait_and_invalidate();
    }
}

void Cache::read_cache(CacheLine* c_set, uint64_t block_addr, uint64_t addr, uint64_t index) {
    while (bus_lock != my_id) {
        wait_and_invalidate();
    }


    VERBOSE && cout << "--------- Cache id " << my_id << " acquired the bus ---------" << endl;

    bool cache_hit = is_cache_hit(c_set, block_addr);
    cacheController->update(addr, my_id, FUNC_READ, cache_hit, trans_id_ctr);

    wait(Port_CCTransId.value_changed_event());

    if(!cache_hit) {
        wait(100);
        insert(c_set, block_addr, addr, index);
    } else {
        refresh_lu_time(c_set, block_addr, addr, index);
    }

    trans_id_ctr++;
    bus_lock = (bus_lock + 1) % num_cpus; // Release the lock
    num_requests_before_me = 0;


    while (bus_lock != 0) {
        wait_and_invalidate();
    }
}

void Cache::execute() {
    while (true) {
        wait(Port_Func.value_changed_event());

        Function f = Port_Func.read();
        uint64_t addr = Port_Addr.read();
        uint64_t block_addr = addr / LINE_SIZE;
        uint64_t data = 0;

        size_t set_index = block_addr % N_SETS;
        CacheLine* c_set = line_table[set_index];

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
            Port_Data.write(0);
            Port_Done.write(RET_READ_DONE);
            wait();
            Port_Data.write(float_64_bit_wire);
        } else if (f == FUNC_WRITE) {
            Port_Done.write(RET_WRITE_DONE);
        } else {
            Port_Done.write(RET_NOP_DONE);
        }
    }
}
