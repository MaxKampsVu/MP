#include <systemc.h>

#ifndef BUS_SLAVE_IF_H
#define BUS_SLAVE_IF_H

/* NOTE: Although this model does not have a bus, this bus slave interface is
 * implemented by the memory. The Cache uses this bus slave interface to
 * communicate directly with the memory. */
class bus_slave_if : public virtual sc_interface {
    public:
    virtual void read(uint64_t addr, uint64_t trans_id, uint64_t cache_id) = 0;
    virtual void write(uint64_t addr, uint64_t trans_id, uint64_t cache_id) = 0;
};

#endif
