#!/bin/bash

# Configure the MTU sizes of all the network interfaces.

# Usage: this-script.sh <mtu_in_bytes>

# UPDATE the target interfaces here. The below ifaces are for "nvidarm" DPU.
IFACES=("pf0hpf" "p0" "ovsbr1" "en3f0pf0sf0" "enp3s0f0s0")

MTU_SIZE=$1

if [ -z "$MTU_SIZE" ]; then
    echo "Error: MTU size is not set."
    echo "Usage: this-script.sh <mtu_in_bytes>"
    exit 1
fi

for IFACE in "${IFACES[@]}"; do
    sudo ip link set dev "$IFACE" mtu "$MTU_SIZE"
done
