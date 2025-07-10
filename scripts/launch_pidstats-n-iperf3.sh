#!/bin/bash

# Bidirectional traffic, report only at the end (-i 60)
# This is an example for the "nvidarm" Host.
numactl -C 20-39 --membind=1 iperf3 -B 129.57.177.126 -c 129.57.177.6 --bidir -P 8 -t 60 -i 60 &
pid=$!
pidstat -p $pid 1 > pidstat.log  # monitor interval is 1
