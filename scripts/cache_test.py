#!/usr/bin/env python3

from trace_lib import Trace

t = Trace(__file__.replace('.py', '.trf'), 1)

# Run with 8B cache, 4 sets, 1B lines 


t.read(0) 
t.write(0)
t.write(0)
t.read(0)

t.write(1)
t.read(1)





t.close()

