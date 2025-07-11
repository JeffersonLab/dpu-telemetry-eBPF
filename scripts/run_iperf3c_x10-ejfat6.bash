#!/bin/bash

OUTFILE="iperf3_udp_summary.log"
> "$OUTFILE"  # Clear previous results

for i in {1..10}; do
    echo "Run $i..."
    
    taskset -c 80-95 iperf3 -c 129.57.177.126 -B 129.57.177.6 -u -l 8948 -b 10G -P 4 -t 10 \
        | grep "\[SUM\].*receiver" >> "$OUTFILE"
    sleep 1
done

