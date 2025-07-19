#!/bin/bash

for p in 20 200 2000 4000; do
    for option in u r d; do
        echo "Run with $p Hz, -$option"
        bash launch_pidstats-n-userspace.sh $p $option
        sleep 5
        echo
    done
done
