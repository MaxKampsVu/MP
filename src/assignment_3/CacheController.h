#ifndef CACHE_CONTROLLER_H
#define CACHE_CONTROLLER_H
#define SC_ALLOW_DEPRECATED_IEEE_API

#include <iostream>
#include <systemc.h>
#include <string>
#include <list>

static bool CONTROLLER_VERBOSE = true;
static uint64_t INVALID_CACHE_ID = 999999;

class Cache; 

enum State {Invalid, Exclusive, Modified, Owned, Shared};

struct CacheState {
    uint64_t cache_id;
};

struct AddrGroup {
    uint64_t addr;
    uint64_t modified_id;
    uint64_t owner_id;
    State group_state;
    list<CacheState*> cacheStates;
};

SC_MODULE(CacheController)  {
public:
    // Refrence to caches 
    std::vector<Cache*> caches;

    // Ports to caches 
    sc_out<uint64_t> Port_CacheTransId;
    sc_out<uint64_t> Port_CacheCacheId;


    SC_CTOR(CacheController) {
        caches.resize(NUM_CPUS);
    }

    // Destructor
    ~CacheController() {

    }

    // Public Methods

    void invalidate_members(AddrGroup *group, uint64_t cache_id) {
        for (auto it = group->cacheStates.begin(); it != group->cacheStates.end(); ++it) {
            if ((*it)->cache_id != cache_id) {
                caches[(*it)->cache_id]->invalidate(group->addr);
            }
        }
    }

    void update(uint64_t addr, uint64_t cache_id, Function func, bool is_hit, uint64_t trans_id) {
        VERBOSE && cout << "--------- Cache Controller ---------" << endl;
        cout << "Cache controller received operation: cache: " << cache_id << " | addr: " << addr << " | func: " << func_to_str(func) << " | cache hit: " << is_hit << endl;

        AddrGroup *group = findAddrGroup(addr);

        // First insertion: the addr is exclusive
        if (group == nullptr) { 
            if (func == FUNC_READ) {
                CONTROLLER_VERBOSE && cout << "Cache: " << cache_id << " is in state exclusive for addr: " << addr << endl;
                std::list<CacheState*> cacheStateList = {new CacheState{cache_id}};
                AddrGroup newGroup{addr, INVALID_CACHE_ID, INVALID_CACHE_ID, Exclusive, cacheStateList};
                system_states.push_back(newGroup);  
            } else {
                //Seg fault is here
                std::list<CacheState*> cacheStateList = {new CacheState{cache_id}};
                AddrGroup newGroup{addr, INVALID_CACHE_ID, INVALID_CACHE_ID, Modified, cacheStateList};
                system_states.push_back(newGroup);  
            }
            
        } else {
        // Atleast one other cache has the cache line
            switch (group->group_state)
            {
            case Shared:
                handle_shared(group, cache_id, func, is_hit);
                break;
            case Exclusive:
                handle_exlusive(group, cache_id, func, is_hit);
                break;
            case Modified:
                handle_modified(group, cache_id, func, is_hit);
                break;
            case Owned: 
                handle_owned(group, cache_id, func, is_hit);
                break;
            default:
                break;
            };
        }
        CONTROLLER_VERBOSE && cout << "Cache controller finished" << endl;  
        Port_CacheTransId.write(trans_id);
        Port_CacheCacheId.write(cache_id);
    }

    void handle_shared(AddrGroup *group, uint64_t cache_id, Function func, bool is_hit) {
        // Read miss -> append cache_id to the shared group 
        if (func == FUNC_READ) {
            if(is_hit) {
                return;
            } else {
                CONTROLLER_VERBOSE && cout << "Cache: " << cache_id << " transitioned to state shared for addr: " << group->addr << endl;  
                group->cacheStates.push_back(new CacheState{cache_id});
            }
        } 
        // Write miss -> invalidate all cache besides cache_id and create a modified group with just cache_id 
        else {
            if(is_hit) {
                //trigger write back to memory 
                invalidate_members(group, cache_id); // invalidate all but one member 
                CONTROLLER_VERBOSE && cout << "Write back addr: " << group->addr << " to memory" << endl;
                // TODO: writeback to bus
                make_modified(group, cache_id, func);
            } else {
                //Think about this case 
                invalidate_members(group, INVALID_CACHE_ID); // invalidate all members 
                CONTROLLER_VERBOSE && cout << "Write back addr: " << group->addr << " to memory" << endl;
            
                make_modified(group, cache_id, func);
            }
        }
    }

    void handle_exlusive(AddrGroup *group, uint64_t cache_id, Function func, bool is_hit) {
        // Read miss -> create a shared group 
        if (func == FUNC_READ) {
            if(is_hit) {
                return;
            }

            CONTROLLER_VERBOSE && cout << "Addr: " << group->addr << " transitioned from exclusive to shared " <<  endl;  
            group->cacheStates.push_back(new CacheState{cache_id});
            //TODO: call insert on cache_id
            CONTROLLER_VERBOSE && cout << "Cache: " << cache_id << " is in state shared for addr: " << group->addr << endl;  
            group->group_state = Shared;
        } 
        // Write miss -> create a modified group 
        else {
            make_modified(group, cache_id, func);
        }
    }

    void handle_modified(AddrGroup *group, uint64_t cache_id, Function func, bool is_hit) {
        //Stay in state modified 
        if(cache_id == group->modified_id) {
            //Do nothing
            return;
        } 
        
        if(is_hit) {
            //Transition to state owned 
            if(func == FUNC_READ) {
                group->group_state = Owned;
                group->owner_id = group->modified_id;
                group->modified_id = INVALID_CACHE_ID;
                CONTROLLER_VERBOSE && cout << "Addr: " << group->addr << " transitioned from modified to owned by cache: " << group->owner_id <<  endl;  
            } 
            // Write back to memory and invalidate all but cache_id
            else {
                invalidate_members(group, cache_id);
                remove_group(group);
            }
        } 
        // Cache miss 
        else {
            // Cache to cache transfer 
            if(func == FUNC_READ) {
                group->group_state = Owned;
                group->owner_id = group->modified_id;
                group->modified_id = INVALID_CACHE_ID;
                group->cacheStates.push_back(new CacheState{cache_id});
                CONTROLLER_VERBOSE && cout << "Addr: " << group->addr << " transitioned from modified to owned by cache: " << group->owner_id <<  endl;  
            } else {
                // trigger write back
            }
        }
    }

    // Write miss -> invalidate all cache besides cache_id and create a modified group with just cache_id
    void make_modified(AddrGroup *group, uint64_t cache_id, Function func) {
        CONTROLLER_VERBOSE && cout << "Cache: " << cache_id << " is in state modified for addr: " << group->addr << endl;  
        std::list<CacheState*> cacheStateList = {new CacheState{cache_id}};
        group->cacheStates = cacheStateList;
        group->modified_id = cache_id; // mark cache_id as the cache that modified the address 
        group->group_state = Modified;
    }

    void handle_owned(AddrGroup *group, uint64_t cache_id, Function func, bool is_hit) {
        if (func == FUNC_READ) {
            // do nothing
            return; 
        } 

        if(is_hit) {
            if (group->owner_id == cache_id) {
                group->owner_id = INVALID_CACHE_ID;
                group->modified_id = cache_id;
                group->group_state = Modified;
                CONTROLLER_VERBOSE && cout << "Addr: " << group->addr << " transitioned from owned to modified by cache: " << group->modified_id <<  endl;  
            } else {
                CONTROLLER_VERBOSE && cout << "Addr: " << group->addr << " transitioned from owned to invalid " << endl;  
                invalidate_members(group, cache_id);
                remove_group(group);
            }
        }
    }

private:
    list<AddrGroup> system_states; 

    std::string func_to_str(uint64_t op) {
        return op ? "write" : "read";
    }

    void remove_group(AddrGroup *group) {
        for (auto it = system_states.begin(); it != system_states.end(); ++it) {
            if (&(*it) == group) {
                system_states.erase(it);
                break;  
            }
        } 
    }

    AddrGroup* findAddrGroup(uint64_t addr) {
        // Iterate through each address group in the system states
        for (auto& group : system_states) {
            if (group.addr == addr) {
                return &group;
            }
        }
        return nullptr;  // Return nullptr if address is not found
    }

    bool cache_in_group(AddrGroup *group, uint64_t cache_id) {
        for (auto it = group->cacheStates.begin(); it != group->cacheStates.end(); ++it) {
            if ((*it)->cache_id == cache_id) {
                caches[(*it)->cache_id]->invalidate(group->addr);
                return true;
            }
        }
        return false;
    }
};

#endif

