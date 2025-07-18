#!/bin/bash

# Bidirectional traffic, report only at the end (-i 60)
# This is an example for the "nvidarm" Host.

# UPDATE this line before usage
taskset -c 80-95 iperf3 -B 129.57.177.6 -c 129.57.177.126 --bidir -P 8 -t 60 -i 60 &

iperf_pid=$!

echo "Started iperf3 with PID $iperf_pid"
echo "Monitoring all iperf3-related CPU usage..."

# Start monitoring all iperf3 processes with pidstat
pidstat -p $iperf_pid 1 > pidstat.log &   # 1 is monitor interval
# pidstat -C iperf3 1 > pidstat.log &   # monitor all iperf3 threads, not by pid. Similar results
pidstat_pid=$!

# Wait for iperf3 to finish
wait $iperf_pid

# After iperf3 exits, stop pidstat
kill $pidstat_pid
echo "iperf3 finished. pidstat log saved to pidstat.log"
