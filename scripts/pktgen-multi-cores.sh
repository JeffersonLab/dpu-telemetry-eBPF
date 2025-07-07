#!/bin/bash

# Use core $START_CORE_NUM-$END_CORE_NUM to generate UDP packeets at maximum speed. 

# Adjust these
SRC_IFACE=$1   # find the 100+ Gbps Ethernet one on local host
START_CORE_NUM=$2  # 20 for "nvidarm"
END_CORE_NUM=$3  # Inclusive. 39 for "nvidarm"

# Calculate number of threads
THREADS=$((${END_CORE_NUM} - ${START_CORE_NUM} + 1))
echo "Total threads: $THREADS"

DST_IP=$4    # destination IP address, get from "ip -br a"
DST_MAC=$5    # destination MAC address, get from "ip link show <iface>"

PKT_SIZE=60     # IP+UDP = 28B, L2 header=14B => 60B payload = 64B total
PKT_COUNT=1000000   # NUmber of packets to send per core

# Clean previous config
for ((i=0; i<=${END_CORE_NUM}; i++)); do
  echo "rem_device_all" > /proc/net/pktgen/kpktgend_$i
done

# Add devices
for ((i=${START_CORE_NUM}; i<=${END_CORE_NUM}; i++)); do
  # echo "Adding device ${SRC_IFACE}@${i} to core $i"
  echo "add_device ${SRC_IFACE}@${i}" > /proc/net/pktgen/kpktgend_${i}
done

# Setup each device queue
for ((i=${START_CORE_NUM}; i<=${END_CORE_NUM}; i++)); do
  DEV="${SRC_IFACE}@${i}"
  echo "Configuring $DEV"
  # clone_skb 1 means reuse skb structure
  echo "clone_skb 0" > /proc/net/pktgen/${DEV}
  echo "pkt_size $PKT_SIZE" > /proc/net/pktgen/${DEV}
  echo "count $PKT_COUNT" > /proc/net/pktgen/${DEV}
  echo "delay 0" > /proc/net/pktgen/${DEV}
  echo "dst_mac $DST_MAC" > /proc/net/pktgen/${DEV}
  echo "dst $DST_IP" > /proc/net/pktgen/${DEV}
  echo "flag QUEUE_MAP_CPU" > /proc/net/pktgen/${DEV}
done

# Start all threads
echo "Running pktgen on cores ${START_CORE_NUM} to ${END_CORE_NUM}"
echo "start" > /proc/net/pktgen/pgctrl

# Result stats
echo "All threads completed."
echo
for ((i=${START_CORE_NUM}; i<=${END_CORE_NUM}; i++)); do
  DEV="${SRC_IFACE}@${i}"
  # echo "=== Stats for $DEV ==="
  statline=$(tail -n 1 /proc/net/pktgen/${DEV})
  pps=$(echo "$statline" | grep -oP '\d+(?=pps)')
  echo "$pps"
done
