#!/bin/bash

# Usage: run this on nvidarm host with $IFACE to check the TX queue to CPU core mapping.
# XPS: transmit packet steering.

# Output on nvidarm Host, NUMA cores 20-39 mapped to TX-queue 0-19.
# /sys/class/net/enP2s1f0np0/queues/tx-0/xps_cpus: 20
# /sys/class/net/enP2s1f0np0/queues/tx-10/xps_cpus: 30
# /sys/class/net/enP2s1f0np0/queues/tx-11/xps_cpus: 31
# /sys/class/net/enP2s1f0np0/queues/tx-12/xps_cpus: 32
# /sys/class/net/enP2s1f0np0/queues/tx-13/xps_cpus: 33
# /sys/class/net/enP2s1f0np0/queues/tx-14/xps_cpus: 34
# /sys/class/net/enP2s1f0np0/queues/tx-15/xps_cpus: 35
# /sys/class/net/enP2s1f0np0/queues/tx-16/xps_cpus: 36
# /sys/class/net/enP2s1f0np0/queues/tx-17/xps_cpus: 37
# /sys/class/net/enP2s1f0np0/queues/tx-18/xps_cpus: 38
# /sys/class/net/enP2s1f0np0/queues/tx-19/xps_cpus: 39
# /sys/class/net/enP2s1f0np0/queues/tx-1/xps_cpus: 21
# /sys/class/net/enP2s1f0np0/queues/tx-20/xps_cpus: 0
# /sys/class/net/enP2s1f0np0/queues/tx-21/xps_cpus: 1
# /sys/class/net/enP2s1f0np0/queues/tx-22/xps_cpus: 2
# /sys/class/net/enP2s1f0np0/queues/tx-23/xps_cpus: 3
# /sys/class/net/enP2s1f0np0/queues/tx-24/xps_cpus: 4
# /sys/class/net/enP2s1f0np0/queues/tx-25/xps_cpus: 5
# /sys/class/net/enP2s1f0np0/queues/tx-26/xps_cpus: 6
# /sys/class/net/enP2s1f0np0/queues/tx-27/xps_cpus: 7
# /sys/class/net/enP2s1f0np0/queues/tx-28/xps_cpus: 8
# /sys/class/net/enP2s1f0np0/queues/tx-29/xps_cpus: 9
# /sys/class/net/enP2s1f0np0/queues/tx-2/xps_cpus: 22
# /sys/class/net/enP2s1f0np0/queues/tx-30/xps_cpus: 10
# /sys/class/net/enP2s1f0np0/queues/tx-31/xps_cpus: 11
# /sys/class/net/enP2s1f0np0/queues/tx-32/xps_cpus: 12
# /sys/class/net/enP2s1f0np0/queues/tx-33/xps_cpus: 13
# /sys/class/net/enP2s1f0np0/queues/tx-34/xps_cpus: 14
# /sys/class/net/enP2s1f0np0/queues/tx-35/xps_cpus: 15
# /sys/class/net/enP2s1f0np0/queues/tx-36/xps_cpus: 16
# /sys/class/net/enP2s1f0np0/queues/tx-37/xps_cpus: 17
# /sys/class/net/enP2s1f0np0/queues/tx-38/xps_cpus: 18
# /sys/class/net/enP2s1f0np0/queues/tx-39/xps_cpus: 19
# /sys/class/net/enP2s1f0np0/queues/tx-3/xps_cpus: 23
# /sys/class/net/enP2s1f0np0/queues/tx-40/xps_cpus: 40
# /sys/class/net/enP2s1f0np0/queues/tx-41/xps_cpus: 41
# /sys/class/net/enP2s1f0np0/queues/tx-42/xps_cpus: 42
# /sys/class/net/enP2s1f0np0/queues/tx-43/xps_cpus: 43
# /sys/class/net/enP2s1f0np0/queues/tx-44/xps_cpus: 44
# /sys/class/net/enP2s1f0np0/queues/tx-45/xps_cpus: 45
# /sys/class/net/enP2s1f0np0/queues/tx-46/xps_cpus: 46
# /sys/class/net/enP2s1f0np0/queues/tx-47/xps_cpus: 47
# /sys/class/net/enP2s1f0np0/queues/tx-48/xps_cpus: 48
# /sys/class/net/enP2s1f0np0/queues/tx-49/xps_cpus: 49
# /sys/class/net/enP2s1f0np0/queues/tx-4/xps_cpus: 24
# /sys/class/net/enP2s1f0np0/queues/tx-50/xps_cpus: 50
# /sys/class/net/enP2s1f0np0/queues/tx-51/xps_cpus: 51
# /sys/class/net/enP2s1f0np0/queues/tx-52/xps_cpus: 52
# /sys/class/net/enP2s1f0np0/queues/tx-53/xps_cpus: 53
# /sys/class/net/enP2s1f0np0/queues/tx-54/xps_cpus: 54
# /sys/class/net/enP2s1f0np0/queues/tx-55/xps_cpus: 55
# /sys/class/net/enP2s1f0np0/queues/tx-56/xps_cpus: 56
# /sys/class/net/enP2s1f0np0/queues/tx-57/xps_cpus: 57
# /sys/class/net/enP2s1f0np0/queues/tx-58/xps_cpus: 58
# /sys/class/net/enP2s1f0np0/queues/tx-59/xps_cpus: 59
# /sys/class/net/enP2s1f0np0/queues/tx-5/xps_cpus: 25
# /sys/class/net/enP2s1f0np0/queues/tx-60/xps_cpus: 60
# /sys/class/net/enP2s1f0np0/queues/tx-61/xps_cpus: 61
# /sys/class/net/enP2s1f0np0/queues/tx-62/xps_cpus: 62
# /sys/class/net/enP2s1f0np0/queues/tx-6/xps_cpus: 26
# /sys/class/net/enP2s1f0np0/queues/tx-7/xps_cpus: 27
# /sys/class/net/enP2s1f0np0/queues/tx-8/xps_cpus: 28
# /sys/class/net/enP2s1f0np0/queues/tx-9/xps_cpus: 29

for f in /sys/class/net/${IFACE}/queues/tx-*/xps_cpus; do
  echo -n "$f: "
  IFS=, read a b c < "$f"
  printf "%08x%08x%08x\n" 0x$a 0x$b 0x$c | tac | tr -d '\n' | \
    xargs -I{} bash -c 'echo "ibase=16; obase=2; {}" | bc' | \
    rev | grep -bo 1 | cut -d: -f1
done
