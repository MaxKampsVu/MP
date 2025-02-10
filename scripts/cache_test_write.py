#!/usr/bin/env python3

from trace_lib import Trace

t = Trace(__file__.replace('.py', '.trf'), 1)

# Run with 8B cache, 4 sets, 1B lines 


t.write(0) 
t.write(8) 
t.read(0) # 0 gets refreshed 
t.write(16) # evict 8 to make space 

t.write(1) 
t.write(9) 
t.write(17) # evict 1 to make space




t.close()

