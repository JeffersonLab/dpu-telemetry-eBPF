#/bin/bash

# Set iface as an env variable first

# Usage: sudo IFACE=xxx bash <this-script>.sh <ingress | egress> $SEC_NAME $HOOK_ID

# Check if IFACE is set
if [ -z "$IFACE" ]; then
    echo "Error: IFACE environment variable is not set."
    echo "Please set it using: export IFACE=<interface_name>"
    exit 1
fi

# Continue with the script
echo "Using interface: $IFACE"

DIRECTION=$1
SEC_NAME=$2

# Delete "pref $HOOK_ID" for now since it's error-probe to cause orphan hooks without carefully detach
sudo tc qdisc add dev $IFACE clsact
sudo tc filter add dev $IFACE ${DIRECTION} \
    bpf da obj ../traffic_counter/kernel_${DIRECTION}_tc.o sec ${SEC_NAME}

