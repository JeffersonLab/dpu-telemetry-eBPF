# TC ingress and userspace collector

This setup counts incoming IPv4 TCP/UDP traffic with `kernel_ingress_tc.c`, pins its map, and runs `tc_collector` to write per-edge samples to Redis. Repeat it on each Linux node whose incoming traffic you want to observe.

For central Redis/backend startup and the local frontend, see the [eCenter setup guide](https://github.com/cissieAB/eCenter/blob/main/docs/setup.md). For the data format and behavior, see [Traffic collection](../../docs/traffic-collection.md). Start Redis before running the collector.

## Prerequisites

Use Linux with BPF/TC support and administrator privileges for attaching programs and opening maps. Install Clang/LLVM with a BPF target, Linux headers, libbpf development files, `bpftool`, `tc`, CMake, a C++17 compiler, a build tool, and hiredis development files.

For DNF-based systems, an example installation is:

```bash
sudo dnf install -y clang llvm libbpf-devel bpftool iproute kernel-headers cmake gcc-c++ make hiredis-devel
```

Package names and repository availability vary by distribution. On other Linux distributions, install equivalent packages. On Debian/Ubuntu, `sudo apt install linux-source` provides the kernel headers referenced by the BPF compile. The BPF filesystem must be mounted at `/sys/fs/bpf` before pinning maps.

## Compile and attach

Replace `<telemetry-repo>` with the absolute path of this repository and `<interface>` with the interface carrying the incoming traffic. Keep the following steps in the same terminal so the variables remain available.

```bash
cd <telemetry-repo>/eCounter/v1_userspace-poll
ip link show
TELEMETRY_IFACE='<interface>'
TELEMETRY_MAP_PATH='/sys/fs/bpf/tc-ing'
clang -O2 -g -target bpf -c kernel_ingress_tc.c -o kernel_ingress_tc.o
```

`-g` is required so the object carries the BTF debug information that `bpftool` uses to pretty-print map keys. If architecture-specific headers such as `asm/types.h` cannot be found, install the matching headers and add their actual include directory with `-I` (for example `-I/usr/include/aarch64-linux-gnu` on the DPU). That path is an example, not a portable default.

Inspect the interface before attaching:

```bash
sudo tc qdisc show dev "$TELEMETRY_IFACE"
sudo tc filter show dev "$TELEMETRY_IFACE" ingress
```

Add `clsact` only if it is absent:

```bash
sudo tc qdisc add dev "$TELEMETRY_IFACE" clsact
```

Attach the program once, then verify:

```bash
sudo tc filter add dev "$TELEMETRY_IFACE" ingress bpf da obj kernel_ingress_tc.o sec tc-ing
sudo tc filter show dev "$TELEMETRY_IFACE" ingress
sudo bpftool map show name map_in_tc
```

The object uses section `tc-ing` and map name `map_in_tc`. The section name passed to `tc filter` must match the `SEC(...)` string in the kernel source. If the collector program is already attached, inspect that attachment before adding another copy.

### Alternative: attach via XDP

`kernel_ingress_xdp.c` provides the same counting on the XDP ingress hook (section `xdp-ing`, map `map_in_xdp`). XDP *driver* (native) mode requires an MTU of at most 3498; above that the attach is rejected and you must fall back to `xdpgeneric`, which is slower than TC.

```bash
sudo ip link set "$TELEMETRY_IFACE" mtu 3498          # optional, enables driver mode
sudo ip link set dev "$TELEMETRY_IFACE" xdp obj kernel_ingress_xdp.o sec xdp-ing
ip link show dev "$TELEMETRY_IFACE"
```

An attached link shows a `prog/xdp` line; a link without XDP does not:

```bash
# With XDP (driver mode) attached
4: enP2s1f0np0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 3498 xdp qdisc mq state UP mode DEFAULT group default qlen 1000
    link/ether 08:c0:eb:f1:5c:58 brd ff:ff:ff:ff:ff:ff
    prog/xdp id 375
    altname enP2p1s0f0np0

# Without XDP
4: enP2s1f0np0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 3498 qdisc mq state UP mode DEFAULT group default qlen 1000
    link/ether 08:c0:eb:f1:5c:58 brd ff:ff:ff:ff:ff:ff
    altname enP2p1s0f0np0
```

If the attach fails, check `sudo dmesg | grep -i xdp` — an MTU that is too large for driver mode is reported there. Pin `map_in_xdp` to a path of its own (for example `/sys/fs/bpf/map_in_xdp`) and pass it with `--map-path`.

## Pin the map

Locate the map, then pin it by name or by ID:

```bash
sudo bpftool map show name map_in_tc
sudo bpftool map pin name map_in_tc "$TELEMETRY_MAP_PATH"
sudo bpftool map show pinned "$TELEMETRY_MAP_PATH"
sudo bpftool map dump pinned "$TELEMETRY_MAP_PATH"
```

Pinning is not optional: without it the userspace collector can end up opening a *second*, separate map instance and will never see any traffic counters.

If multiple maps have the same name, identify the intended attachment's map and pin it by ID instead (`sudo bpftool map pin id <id> <path>`). If the pin path exists already, verify it refers to the current attachment's map before reusing it. Use distinct pin paths for separate interfaces.

## Build and run

From the same directory, build the userspace collector:

```bash
cmake -S . -B build
cmake --build build
sudo ./build/tc_collector --redis-host <redis-host> --poll-hz 20 --map-path "$TELEMETRY_MAP_PATH"
```

Replace `<redis-host>` with the reachable hostname or IP of the Redis machine. Use `localhost` if Redis is published on this node. The Compose hostname `redis` is for containers on the Compose network, not collectors on other hosts. The Redis address can differ from the data-network addresses observed in packets.

For later runs, use the existing binary directly:

```bash
cd <telemetry-repo>/eCounter/v1_userspace-poll
sudo ./build/tc_collector --redis-host <redis-host> --poll-hz 20 --map-path /sys/fs/bpf/tc-ing
```

Adjust the pin path if you chose a different one. Rebuild after source changes. If the shared map layout in `tc_common.h` changes, rebuild both the kernel object and userspace binary and attach/pin a fresh matching map.

## Collector options

| Option | Default | Meaning |
| --- | --- | --- |
| `-p`, `--poll-hz` | `20` | Samples per second; must be a positive divisor of 1,000,000. |
| `-m`, `--map-path` | `/sys/fs/bpf/tc-eg` | Pinned map path. Explicitly pass `/sys/fs/bpf/tc-ing` for this ingress setup. |
| `--redis-host` | `localhost` | Redis hostname or IP. |
| `--redis-port` | `6379` | Redis port, from 1 through 65535. |
| `--redis-ttl` | `3600` | Positive record retention time in seconds. |
| `-v`, `--verbose` | Off | Log successfully published Redis keys. |

The collector writes `packet:<dest_ip>:<source_ip>:<timestamp>` hashes to Redis database 0. There is no Redis database selector in this CLI. Keep the backend on the same database.

## Verify

### Inspect the map directly

Generate IPv4 TCP/UDP traffic into the selected interface from another node, then dump the map in another terminal — this works with or without the collector running:

```bash
sudo bpftool map dump pinned /sys/fs/bpf/tc-ing
```

```json
[{
        "key": {
            "source_ip": 112277889,
            "destination_ip": 2122317504,
            "proto": 6,
            "pad": [0,0,0]
        },
        "value": {
            "packets": 173416415,
            "bytes": 257392610459
        }
    }
]
```

IP addresses are printed as the raw network-byte-order `__u32`, and `proto` is 6 for TCP, 17 for UDP. Counters should change for the expected source/destination pair. Use your chosen pin path if different.

### Test with `nc` (low speed)

A single `nc` stream is the simplest end-to-end check:

1. On the monitored host, start a UDP listener in keep-listening mode: `nc -l -u -k <port_number>`.
2. From another node, send UDP traffic to the monitored interface's IPv4 address: `nc -u <monitored_ipv4> <port_number>`.

Type a few lines into the sender. Each one should bump `packets`/`bytes` for the matching `(source_ip, destination_ip, proto=17)` key in the map dump, and — with `--verbose` on the collector — produce `Published Redis key: packet:<dest>:<src>:<ts>` lines.

### Test with `iperf3`

For sustained TCP/UDP load, see the [iperf3 testing guide](../../docs/iperf3.md). Run the server on the monitored node and send traffic from another node to the monitored interface's IPv4 address.

The backend topology must contain the observed IPs for the intended graph layout; those IPs can differ from management addresses.

### Troubleshooting

- **Compile fails:** check BPF target support, development packages, and architecture-specific include paths.
- **Map cannot be opened:** check the attachment, pin path, and privileges. The collector's default path is not this guide's ingress path.
- **Map stays empty:** check the selected interface and that incoming traffic is IPv4 TCP/UDP.
- **Map changes but no Redis records:** inspect collector logs, Redis address/port, and connectivity. An initial Redis connection failure exits; runtime write failures are logged.
- **Graph stays empty:** check collector output, backend database and topology, and that traffic is still arriving.

## Stop and clean up

Stop the collector with `Ctrl+C`. This leaves the kernel attachment and map pin in place for reuse.

For a full teardown, remove only this run's pin and hook. Unpin first — a pinned map stays alive and keeps the program attached:

```bash
sudo rm /sys/fs/bpf/tc-ing                                   # or your chosen pin path
```

Then detach. For TC:

```bash
sudo tc filter del dev "$TELEMETRY_IFACE" ingress            # see the caution below
sudo tc qdisc del dev "$TELEMETRY_IFACE" clsact
```

For XDP, the off mode must match the mode used to attach:

```bash
sudo ip link set dev "$TELEMETRY_IFACE" xdp off              # or xdpgeneric off
```

On a shared interface, do not use the blanket `tc filter del dev ... ingress` above — it removes every ingress filter. Identify this run's filter with `sudo tc filter show dev "$TELEMETRY_IFACE" ingress` and delete it by handle/priority instead. Likewise remove `clsact` only if no other filters need it.

Finally, confirm the map is gone:

```bash
sudo bpftool map show
```
