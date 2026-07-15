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
4. Each active edge-second is written to Redis as a hash with a configurable
   TTL.

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
- Every emitted edge contains four arrays of exactly `samples_per_second`
  elements. Inactive protocols and unsampled positions in a partial second are
  represented by zeros. Extra polls are ignored rather than written out of
  bounds.
- Every emitted edge includes its Unix-second `timestamp`,
  `samples_per_second`, `total_packets`, and `total_bytes`; totals are computed
  from the emitted arrays.
- Redis keys use `packet:<dest_ip>:<source_ip>:<timestamp>`. Hash fields match
  the simulator contract except for simulator-only `node_id`.
- Redis defaults to `localhost:6379` with a 3600-second TTL and can be changed
  with `--redis-host`, `--redis-port`, and `--redis-ttl`.
- With `--verbose`, standard output reports successfully published Redis keys; full edge
  records are not dumped to the terminal.
- The userspace collector must be rebuilt whenever the shared map key layout
  changes.

## Failure Behavior

- The collector exits if its initial Redis connection cannot be established.
- Runtime Redis write or expiration failures are logged; packet collection
  continues.

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
- Confirm Redis contains `packet:*` hashes with the expected fields and TTL.
