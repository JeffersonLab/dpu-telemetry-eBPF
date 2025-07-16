#!/bin/bash

# Monitor the qdisc TX packet drop and the current bytes in flight.
# The column headers are TX queue_ids.

IFACE="enP2s1f0np0"
DURATION=60           # seconds to monitor
INTERVAL=1            # seconds between samples
OUTFILE_DROP="tx_drops.out"
OUTFILE_INFLIGHT="tx_inflight.out"

# Flush the contents
> $OUTFILE_DROP
> $OUTFILE_INFLIGHT

# Get list of parent handles for fq_codel (they map to TX queues)
PARENTS=($(tc -s qdisc show dev "$IFACE" | grep "parent :" | sed -n 's/.*parent :\([0-9a-f]\+\).*/\1/p'))

TX_PATH="/sys/class/net/$IFACE/queues"
TX_QUEUES=($(ls -d $TX_PATH/tx-* 2>/dev/null | xargs -n1 basename))

# Write CSV header
echo -n "Time" > "$OUTFILE_DROP"
for p in "${PARENTS[@]}"; do 
    echo -n ",$p" >> "$OUTFILE_DROP";
done
echo >> "$OUTFILE_DROP"

echo -n "Time" > "$OUTFILE_INFLIGHT"
for q in "${TX_QUEUES[@]}"; do
    echo -n ",$q" >> "$OUTFILE_INFLIGHT"
done
echo >> "$OUTFILE_INFLIGHT"

# Monitor loop
for ((i = 0; i < DURATION; i += INTERVAL)); do
    TIMESTAMP=$(date "+%H:%M:%S")
    echo -n "$TIMESTAMP" >> "$OUTFILE_DROP"
    echo -n "$TIMESTAMP" >> "$OUTFILE_INFLIGHT"
    for p in "${PARENTS[@]}"; do
        # Extract dropped packets from qdisc stats
        DROP=$(tc -s qdisc show dev "$IFACE" | awk -v pat="parent :$p " '
        $0 ~ pat {found=1}
        found && /dropped/ {
            match($0, /dropped ([0-9]+)/, m)
            print m[1]
            found=0
        }')
        echo -n ",${DROP:-0}" >> "$OUTFILE_DROP"
    done
    for q in "${TX_QUEUES[@]}"; do
        # Extract in flight bytes within each queue
        VALUE=$(cat "$TX_PATH/$q/byte_queue_limits/inflight" 2>/dev/null || echo 0)
        echo -n ",$VALUE" >> "$OUTFILE_INFLIGHT"
    done
    echo >> "$OUTFILE_DROP"
    echo >> "$OUTFILE_INFLIGHT"
    sleep "$INTERVAL"
done

echo "Drop monitoring complete. Output saved to $OUTFILE_DROP and $OUTFILE_INFLIGHT"
