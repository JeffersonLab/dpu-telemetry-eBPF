#!/bin/bash

# Generate large amount of packets indicated by $PKT_COUNT with a single CPU core.
# Check the output with "sudo cat /proc/net/pktgen/<iface>"
#
# Kernel pktgen reference:
#       https://cm-gitlab.stanford.edu/ntonnatt/linux/-/tree/master/samples/pktgen
#
# Usage:
#       sudo bash <this-script>.sh <src-iface> <src-core> <dst-ip> <dst-mac>
#

# Interface to use
SRC_IFACE=$1   # find the 100+ Gbps Ethernet one on local host
SRC_CORE=$2   # good for NUMA pinning

# Packet parameters
DST_IP=$3    # destination IP address, get from "ip -br a"
DST_MAC=$4    # destination MAC address, get from "ip link show <iface>"

# Minimum Ethernet frame size is 64 bytes? NIC will add 4 bytes of CRC (Cyclic Redundancy Check).
# Minimum value deduced from the sender pktgen output is 42.
# However, view from the receiver side /proc/net/dev, minimum packet size is 60.
# IP+UDP = 28B, L2 header=14B => 60B payload = 64B total
PKT_SIZE=60     # in bytes
PKT_COUNT=1000000
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

# Result stats
statline=$(tail -n 1 /proc/net/pktgen/$SRC_IFACE)
  pps=$(echo "$statline" | grep -oP '\d+(?=pps)')
  echo "$pps"

# xmei@nvidarm:~/tc-metric/scripts$ sudo cat /proc/net/pktgen/enP2s1f0np0 
# Params: count 1000000  min_pkt_size: 60  max_pkt_size: 60
#      frags: 0  delay: 0  clone_skb: 0  ifname: enP2s1f0np0
#      flows: 0 flowlen: 0
#      queue_map_min: 0  queue_map_max: 0
#      dst_min: 129.57.177.6  dst_max: 
#      src_min:   src_max: 
#      src_mac: 08:c0:eb:f1:5c:58 dst_mac: 10:70:fd:df:8e:37
#      udp_src_min: 9  udp_src_max: 9  udp_dst_min: 1234  udp_dst_max: 1234
#      src_mac_count: 0  dst_mac_count: 0
#      Flags: 
# Current:
#      pkts-sofar: 1000000  errors: 0
#      started: 2745005680010us  stopped: 2745006729039us idle: 796us
#      seq_num: 1000001  cur_dst_mac_offset: 0  cur_src_mac_offset: 0
#      cur_saddr: 129.57.177.126  cur_daddr: 129.57.177.6
#      cur_udp_dst: 1234  cur_udp_src: 9
#      cur_queue_map: 0
#      flows: 0
# Result: OK: 1049028(c1048232+d796) usec, 1000000 (60byte,0frags)
#   953262pps 457Mb/sec (457565760bps) errors: 0
