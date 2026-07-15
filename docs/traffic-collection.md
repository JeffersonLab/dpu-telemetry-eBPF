# Traffic Collection

## Purpose

Count observed IPv4 TCP and UDP traffic without blocking packets, then expose
cumulative counters to a userspace collector through pinned BPF maps.

## Main Flow

1. A TC ingress program parses Ethernet and IPv4 headers.
2. TCP and UDP packets are counted by source address, destination address, and
   IP protocol.
3. Userspace polls the pinned map and derives per-interval packet and byte
   deltas for each directed source/destination edge from the cumulative
   counters.

## Expected Behavior

- TC ingress map keys contain `source_ip` as the source IPv4 address,
  `destination_ip` as the destination IPv4 address, and `proto` as the IP
  protocol number.
- Counters contain packet totals and IPv4 `tot_len` byte totals.
- Non-IPv4 and non-TCP/UDP traffic is ignored.
- Packets continue through the networking stack with `TC_ACT_OK`.
- Userspace keeps independent bins and last-seen counters for each directed
  `(source_ip, destination_ip)` edge.
- JSON output identifies edges as `<source_ip>:<dest_ip>` and includes explicit
  dotted-decimal `source_ip` and `dest_ip` fields.
- The userspace collector must be rebuilt whenever the shared map key layout
  changes.

## Key Components

- `eCounter/v1_userspace-poll/kernel_ingress_tc.c`
- `eCounter/v1_userspace-poll/tc_common.h`
- `eCounter/v1_userspace-poll/tc_userspace.cpp`

## Verification

- Compile and attach a fresh TC ingress object and pin its newly created map.
- Confirm `bpftool map dump` shows source address, destination address, and
  protocol fields whose counters increase with observed traffic.
- Rebuild the userspace collector against the same shared header before it
  opens the new map.
