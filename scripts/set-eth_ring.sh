#!/bin/bash

# Configure the RX/TX ring sizes of all the network interfaces.

# Usage: 
    # bash <this_script>.sh TX 1024 RX 1024
    # bash <this_script>.sh TX 1024
    # bash <this_script>.sh RX 2048


# UPDATE the target interfaces here. The below ifaces are for "nvidarm" DPU.
# NOTE: Bridge interfaces such as "ovsbr$X" is not supported.
#       "en3f0pf0sf0" is TX-only.
IFACES=("pf0hpf" "p0" "en3f0pf0sf0" "enp3s0f0s0")

# Default unset values
SET_RX=false
SET_TX=false
RX_SIZE=1024
TX_SIZE=1024

# Usage info
usage() {
  echo "Usage:"
  echo "  $0 TX <value> RX <value>"
  echo "  $0 TX <value>"
  echo "  $0 RX <value>"
  exit 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  key="$1"
  case "$key" in
    TX)
      SET_TX=true
      TX_SIZE="$2"
      shift 2
      ;;
    RX)
      SET_RX=true
      RX_SIZE="$2"
      shift 2
      ;;
    *)
      usage
      ;;
  esac
done

# Validate
if ! $SET_TX && ! $SET_RX; then
  usage
fi

# Apply to interfaces
for IFACE in "${IFACES[@]}"; do
  echo "Configuring interface: $IFACE"

  if $SET_RX; then
    echo "  - Setting RX ring buffer to $RX_SIZE"
    sudo ethtool -G "$IFACE" rx "$RX_SIZE"
  fi

  if $SET_TX; then
    echo "  - Setting TX ring buffer to $TX_SIZE"
    sudo ethtool -G "$IFACE" tx "$TX_SIZE"
  fi

  echo "  - Current ring buffer settings:"
  ethtool -g "$IFACE" | grep -E 'RX|TX'
  echo ""
done
