#!/bin/bash

# Send to ejfat-6 for multiple times
# launch it with numactl -C 20-39 to explicitly set the CPU cores.

# Update before use!!!
DST_MAC=10:70:fd:df:8e:37
DST_IP=129.57.177.6

SRC_IFACE=enP2s1f0np0
SRC_START_CORE_NUM=20
SRC_END_CORE_NUM=39

for i in {1..10}; do    # Update i range before use!!!
  echo "=== Run $i ==="
  sudo bash pktgen-multi-cores.sh \
    $SRC_IFACE $SRC_START_CORE_NUM $SRC_END_CORE_NUM \
        $DST_IP $DST_MAC \
    > test_${i}.out
done
