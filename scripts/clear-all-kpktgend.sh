#!/bin/bash

# Clear all the pktgen setting on all of the cores.

END_CORE_NUM=$1

# Clean previous config
for ((i=0; i<${END_CORE_NUM}; i++)); do
  echo "rem_device_all" > /proc/net/pktgen/kpktgend_$i
done

echo "Completed!"
