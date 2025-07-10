#!/bin/bash

# Bidirectional traffic, report only at the end
numactl -C 80-95 --membind=5 iperf3 -B 129.57.177.6 -c 129.57.177.126 --bidir -P 8 -t 60 -i 60 &
pid=$!
pidstat -p $pid 1 > pidstat.log

