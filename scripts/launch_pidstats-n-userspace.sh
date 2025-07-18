#!/bin/bash
set -ex
# This is an example for the "ejfat-6" server.

# UPDATE these lines before usage

# Adjust parameters before use!!!

FREQ=$1
> user_collector_p_${FREQ}.out
./../traffic_counter/tc_egress_collector.o -p ${FREQ} -m /sys/fs/bpf/tc/tc-eg > user_collector_p_${FREQ}.out &
user_collector_pid=$!

echo "Started user-space program with PID $user_collector_pid"
echo "Monitoring its usage with pidstat..."
echo

OPTION=$2
# pidstat manual: https://www.man7.org/linux/man-pages/man1/pidstat.1.html
# u: CPU usage
# r: RAM usage
# d: disk I/O
> pidstat_${OPTION}_${FREQ}.log
pidstat -${OPTION} -H -p $user_collector_pid 1 > pidstat_${OPTION}_${FREQ}.log &   # 1 is monitor interval
pidstat_pid=$!

# Start an iperf3 client
taskset -c 80-95 /usr/local/bin/iperf3 -B 129.57.177.6 -c 129.57.177.126 -P 8 -t 20 -u -b 1G -l 1400 &


# Wait for user-sapce program to finish
sleep 30
kill $user_collector_pid

# After iperf3 exits, stop pidstat
kill $pidstat_pid
echo "Monitoring finished. pidstat log saved to pidstat_${OPTION}_${FREQ}.log"
