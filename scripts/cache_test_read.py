#!/usr/bin/env python3

from trace_lib import Trace

t = Trace(__file__.replace('.py', '.trf'), 1)

# Run with 8B cache, 4 sets, 1B lines 

t.read(0) # should be a miss 
t.write(0) 
t.read(0) # should be a hit
t.write(4) 
t.write(8) # evict 0 
t.read(0) # should be a miss because 0 got evicted




t.close()

