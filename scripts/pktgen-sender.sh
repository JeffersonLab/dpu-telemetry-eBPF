#!/bin/bash

# Generate large amount of packets indicated by $PKT_COUNT with a single CPU core.
#
# Usage:
#       sudo bash <this-script>.sh <src-iface> <src-core> <dst-ip> <dst-mac>
#
# Check the output with "sudo cat /proc/net/pktgen/<iface>"

# Interface to use
SRC_IFACE=$1   # find the 100+ Gbps Ethernet one on local host
SRC_CORE=$2   # good for NUMA pinning

# Packet parameters
DST_IP=$3    # destination IP address, get from "ip show -br a"
DST_MAC=$4    # destination MAC address, get from "ip link show <iface>"
PKT_COUNT=1000000
PKT_SIZE=64     # bytes including L2 header, corresponds to the minimum Ethernet frame size.
THREADS=1       # number of CPU cores to use

# Clear existing configuration
# The below line is bug-triggering if we do not use kpktgend_x last time
echo "rem_device_all" > /proc/net/pktgen/kpktgend_${SRC_CORE}
 # NUMA node 1-cores 20-39 on "nvidarm"
echo "add_device $SRC_IFACE" > /proc/net/pktgen/kpktgend_${SRC_CORE}

# Configure pktgen device
PGDEV=/proc/net/pktgen/$SRC_IFACE
echo "count $PKT_COUNT"        > $PGDEV
echo "clone_skb 0"             > $PGDEV
echo "pkt_size $PKT_SIZE"      > $PGDEV
echo "delay 0"                 > $PGDEV
echo "dst $DST_IP"             > $PGDEV
echo "dst_mac $DST_MAC"        > $PGDEV
echo "udp_dst_min 1234"        > $PGDEV
echo "udp_dst_max 1234"        > $PGDEV

# Start packet generation
echo "Running pktgen..."
echo "start" > /proc/net/pktgen/pgctrl
