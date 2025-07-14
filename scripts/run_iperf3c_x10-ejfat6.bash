#!/bin/bash

for p in 4 8; do
    OUTFILE="iperf3_P${p}.log"
    > "$OUTFILE"  # Clear previous results once per p


    if [ "$p" -eq 4 ]; then
        b="2G"
    else
        b="1G"
    fi

    echo "Running with P=$p, bitrate=$b"

    for i in {1..10}; do
        echo "Run $i..., payload=1400"
        taskset -c 80-95 iperf3 -c 129.57.177.126 -B 129.57.177.6 \
            -u -b $b -P $p -t 20 -l 1400 \
            | grep "\[SUM\].*receiver" >> "$OUTFILE"
        sleep 5
    done
    echo
done


for p in 4 8; do
    OUTFILE="iperf3_P${p}_speedy.log"
    > "$OUTFILE"  # Clear previous results once per p


    if [ "$p" -eq 4 ]; then
        b="4G"
    else
        b="2G"
    fi

    echo "Running with P=$p, bitrate=$b"

    for i in {1..10}; do
        echo "Run $i..., payload=1400"
        taskset -c 80-95 iperf3 -c 129.57.177.126 -B 129.57.177.6 \
            -u -b $b -P $p -t 20 -l 1400 \
            | grep "\[SUM\].*receiver" >> "$OUTFILE"
        sleep 5
    done
    echo
done

