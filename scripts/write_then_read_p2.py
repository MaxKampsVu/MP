#!/usr/bin/env python3

# Example that shows how to generate a simple two processor trace.

from trace_lib import Trace

t = Trace(__file__.replace('.py', '.trf'), 4)

# 2 processor trace, so generate pairs of events for P0 and P1

# Write some data.

#t.read(0x500)  # P0
#t.read(0x500)  # P1

#t.write(0x500)     # P0
#t.nop();

#t.read(0x500) # P0
#t.read(0x500) # P1

t.read(0x500)  # P0
t.read(0x700)  # P1
t.write(0x700)  # P0
t.write(0x700)  # P1

t.read(0x300)  # P1
t.read(0x300)  # P1
t.nop()  # P1
t.read(0x300)  # P1

t.read(0x400) # P1
t.read(0x400)
t.read(0x400)
t.write(0x400)

t.close()
