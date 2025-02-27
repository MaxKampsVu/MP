#!/usr/bin/env python3

# Example that shows how to generate a simple two processor trace.

from trace_lib import Trace

t = Trace(__file__.replace('.py', '.trf'), 2)

# 2 processor trace, so generate pairs of events for P0 and P1

# Write some data.

t.read(0x500)  # P0
t.read(0x500)  # P1

t.write(0x500)  # P0
t.read(0x500)  # P1

t.read(0x500)  # P0
t.read(0x500)  # P1


t.close()
