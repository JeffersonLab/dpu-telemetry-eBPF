#!/bin/bash

# Compile the kernel codes into ELF objects of the same names.

# Kernel c code
kernels=("kernel_ingress_tc" "kernel_egress_tc" "kernel_ingress_xdp")

# Detect architecture
arch=$(uname -m)
if [[ "$arch" == "aarch64" ]]; then
    INC="-I/usr/include/aarch64-linux-gnu"
else
    INC=""
fi

# Compile each file
for kernel in "${kernels[@]}"; do
    cfile="${kernel}.c"
    obj="${kernel}.o"
    echo "Compiling $cfile -> $obj"
    sudo clang -O2 -g -target bpf $INC -c "$cfile" -o "$obj"
    if [[ $? -ne 0 ]]; then
        echo "Compilation failed for $cfile"
        exit 1
    fi
done

echo "All kernel programs compiled successfully."
