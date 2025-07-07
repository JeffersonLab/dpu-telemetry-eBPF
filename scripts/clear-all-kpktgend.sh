#!/bin/bash

# Clear all the pktgen setting on all of the cores.

TOTAL_CORES=$1

if [ -z "$TOTAL_CORES" ]; then
    echo "Error: core number is not set."
    echo "Usage: this-script.sh <cpu_cores>"
    exit 1
fi

# Clean previous config
for ((i=0; i<${TOTAL_CORES}; i++)); do
  echo "rem_device_all" > /proc/net/pktgen/kpktgend_$i
done

echo "Completed!"
