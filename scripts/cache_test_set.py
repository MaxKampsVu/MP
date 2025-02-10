#!/usr/bin/env python3

from trace_lib import Trace

t = Trace(__file__.replace('.py', '.trf'), 1)

# Run with 8B cache, 4 sets, 1B lines 

# Should all be written in different sets
t.write(0)
t.write(1)
t.write(2)
t.write(3)





t.close()

