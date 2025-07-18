#/bin/bash

# Usage: sudo IFACE=xxx bash <this-script>.sh

# Check if IFACE is set
if [ -z "$IFACE" ]; then
    echo "Error: IFACE environment variable is not set."
    echo "Please set it using: export IFACE=<interface_name>"
    exit 1
fi

# Continue with the script
echo "Using interface: $IFACE"

# FIX: if attached with a hook_id/priority_id (via "pref"), must explicitly dettach with the hook id.

sudo tc filter del dev $IFACE egress
sudo tc filter del dev $IFACE ingress
sudo tc qdisc del dev $IFACE clsact