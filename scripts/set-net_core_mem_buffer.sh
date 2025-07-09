#!/bin/bash

# Set the net.core.rmem and net.core.wmem to 128MiB.
# It does not depend on the network interfaces.

# Target value: 128 MiB. Update before use!
TARGET_BYTES=134217728

# Set receive buffer sizes
sudo sysctl -w net.core.rmem_max=$TARGET_BYTES
sudo sysctl -w net.core.rmem_default=$TARGET_BYTES

# Set send buffer sizes
sudo sysctl -w net.core.wmem_max=$TARGET_BYTES
sudo sysctl -w net.core.wmem_default=$TARGET_BYTES

echo -e "\nVerifying changes..."
sysctl net.core.rmem_max
sysctl net.core.rmem_default
sysctl net.core.wmem_max
sysctl net.core.wmem_default
