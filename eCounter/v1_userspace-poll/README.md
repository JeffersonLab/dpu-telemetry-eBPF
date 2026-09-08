# TC ingress and userspace collector

This setup counts incoming IPv4 TCP/UDP traffic with `kernel_ingress_tc.c`, pins its map, and runs `tc_collector` to write per-edge samples to Redis. Repeat it on each Linux node whose incoming traffic you want to observe.

For central Redis/backend startup and the local frontend, see the [eCenter setup guide](https://github.com/cissieAB/eCenter/blob/main/docs/setup.md). For the data format and behavior, see [Traffic collection](../../docs/traffic-collection.md). Start Redis before running the collector.

## Prerequisites

Use Linux with BPF/TC support and administrator privileges for attaching programs and opening maps. Install Clang/LLVM with a BPF target, Linux headers, libbpf development files, `bpftool`, `tc`, CMake, a C++17 compiler, a build tool, and hiredis development files.

For DNF-based systems, an example installation is:

```bash
sudo dnf install -y clang llvm libbpf-devel bpftool iproute kernel-headers cmake gcc-c++ make hiredis-devel
```

Package names and repository availability vary by distribution. On other Linux distributions, install equivalent packages. The BPF filesystem must be mounted at `/sys/fs/bpf` before pinning maps.

## Compile and attach

Replace `<telemetry-repo>` with the absolute path of this repository and `<interface>` with the interface carrying the incoming traffic. Keep the following steps in the same terminal so the variables remain available.

```bash
cd <telemetry-repo>/eCounter/v1_userspace-poll
ip link show
TELEMETRY_IFACE='<interface>'
TELEMETRY_MAP_PATH='/sys/fs/bpf/tc-ing'
clang -O2 -g -target bpf -c kernel_ingress_tc.c -o kernel_ingress_tc.o
```

If architecture-specific headers such as `asm/types.h` cannot be found, install the matching headers and add their actual include directory with `-I`. The historical architecture-specific path below is an example, not a portable default.

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

The object uses section `tc-ing` and map name `map_in_tc`. If the collector program is already attached, inspect that attachment before adding another copy.

## Pin the map

For a single map named `map_in_tc`:

```bash
sudo bpftool map pin name map_in_tc "$TELEMETRY_MAP_PATH"
sudo bpftool map show pinned "$TELEMETRY_MAP_PATH"
sudo bpftool map dump pinned "$TELEMETRY_MAP_PATH"
```

If multiple maps have the same name, identify the intended attachment's map and pin it by ID instead. If the pin path exists already, verify it refers to the current attachment's map before reusing it. Use distinct pin paths for separate interfaces.

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

## Verify and stop

To generate TCP or UDP traffic for verification, see the [iperf3 testing guide](../../docs/iperf3.md). Run the server on the monitored node and send traffic from another node to the monitored interface's IPv4 address.

Generate IPv4 TCP/UDP traffic into the selected interface from another node, then inspect the map in another terminal:

```bash
sudo bpftool map dump pinned /sys/fs/bpf/tc-ing
```

Use your chosen pin path if different. Counters should change for the expected source/destination pair. Use `--verbose` on the collector to see published keys. The backend topology must contain the observed IPs for the intended graph layout; those IPs can differ from management addresses.

- **Compile fails:** check BPF target support, development packages, and architecture-specific include paths.
- **Map cannot be opened:** check the attachment, pin path, and privileges. The collector's default path is not this guide's ingress path.
- **Map stays empty:** check the selected interface and that incoming traffic is IPv4 TCP/UDP.
- **Map changes but no Redis records:** inspect collector logs, Redis address/port, and connectivity. An initial Redis connection failure exits; runtime write failures are logged.
- **Graph stays empty:** check collector output, backend database and topology, and that traffic is still arriving.

Stop the collector with `Ctrl+C`. This leaves the kernel attachment and map pin available for reuse. For a full teardown, inspect and remove only this run's TC filter and map pin. Remove `clsact` only if no other filters need it; do not use a blanket ingress-filter deletion on a shared interface.

## Historical TC/XDP examples

The original notes below are retained for reference. They describe other programs, machine-specific paths, and output formats; use the setup above for the current TC ingress → Redis workflow. Their broad cleanup commands apply only to an interface dedicated to that example.

## Traffic counter by IPv4 addresses


```bash
sudo apt install linux-source
```

### Compile and run the eBPF TC/XDP program
The below process is test and verified on "nvidarm" with the DPU Ethernet address 129.57.177.126.

1. Compile the eBPF program into an ELF (excutable and linkable) object.
    ```bash
    $ sudo clang -O2 -g -target bpf -I/usr/include/aarch64-linux-gnu -c <kernel_program>.c -o <elf_obj>.o  # "-g" is required to show debug information
    ```
2. Attach the compiled ELF object with XDP/TC hooks.
   
   A. Attach the ELF object to a TC network interface. 
   - Add clsact qdisc to the Ethernet device "net_iface" (printed by `ip link`): `sudo tc qdisc add dev <net_iface> clsact`.
   - Attach the program: `sudo tc filter add dev <net_iface> <ingress | egress> bpf da obj <elf_obj>.o sec <sec_name>`. `<sec_name>` needs to match the "SEC" information in the kernel code.
  
   B. Attach the ELF object to a XDP network interface.
   - (Optional) Set the network interface's MTU to 3498 to enable the XDP *driver* or *native* mode: `sudo ip link set <net_iface> mtu 3498`
   - Attach the compiled program to a network interface "net_iface": `sudo ip link set dev <net_iface> <xdp | xdpgeneric> obj <elf_obj>.o sec <sec_name>`
   - If there is an error, check it with `sudo dmesg | grep -i xdp`. For example, when MTU=9000, `dmesg` will print the information on XDP *native/driver* mode is not allowed, 
   - Verify it with `ip link show dev <net_iface>`.
        ```bash
        # A link device with XDP (driver mode) attached
        nvidarm:~/tc-metric/traffic_counter> ip link show dev enP2s1f0np0
        4: enP2s1f0np0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 3498 xdp qdisc mq state UP mode DEFAULT group default qlen 1000
            link/ether 08:c0:eb:f1:5c:58 brd ff:ff:ff:ff:ff:ff
            prog/xdp id 375  # the XDP line
            altname enP2p1s0f0np0
    
        # Turn off the XDP hook
        nvidarm:~/tc-metric/traffic_counter> sudo ip link set dev enP2s1f0np0 xdp off

        # A normal link device without XDP. No XDP line.
        nvidarm:~/tc-metric/traffic_counter> ip link show dev enP2s1f0np0
        4: enP2s1f0np0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 3498 qdisc mq state UP mode DEFAULT group default qlen 1000
        link/ether 08:c0:eb:f1:5c:58 brd ff:ff:ff:ff:ff:ff
        altname enP2p1s0f0np0
        ```
3. After attaching the hook, inspect the eBPF map without a user space program.
  
    ```bash
    $ sudo bpftool map  # Find the map
    6: lru_hash  name ip_src_map  flags 0x0
    key 4B  value 32B  max_entries 1024  memlock 40960B
    btf_id 150
    ## 6 is the map id; "ip_src_map" is the name which should match our definition in the C eBPF kernel code

    ## If the ebpf map name is truncated, use the truncated one 
    $ sudo bpftool map dump name ip_src_map   # dump the map context
    $ sudo bpftool map dump id 6 # dump by id
    ## Output example
    [{
            "key": {
                "ip": 112277889,
                "proto": 6,
                "pad": [0,0,0
                ]
            },
            "value": {
                "packets": 173416415,
                "bytes": 257392610459
            }
        }
    ]
    ```

4. **PIN** the map for the user space code: `sudo bpftool map pin name <map_name> /sys/fs/bpf/<map_name>`. Can either pin by name or id. Dump this map by pinned address: `sudo bpftool map dump pin /sys/fs/bpf/<map_name>`. *If skipping this step, you might end up openning 2 eBPF map instances when you run the userspace code and never get any traffic stats for your userspace one.*

5. Compile the userspace program: `gcc -o <user_program>.o <user_program>.c -lbpf`
6. Run the userspace program: `sudo <user_program>.o`. The expected output is shown in the next section.

7. **CLEANUP**: **UNPIN** the map and **DELETE** the TC/XDP hooks.
   
   A. If the eBPF map is pinned. Unpin it first.
    ```bash
    # Pinning the map make it persistent and you can not deattach it.
    $ sudo rm /sys/fs/bpf/<map_name>  # delete the pinned map
    ```
   B. Delete the TC rules.
    ```bash
    $ sudo tc filter del dev <net_iface> <ingress | egress>
    $ sudo tc qdisc del dev <net_iface> clsact
    ```
   C. Turn off the XDP hook.
   ```bash
   sudo ip link set dev <net_iface> <xdp | xdpgeneric> off  # must match the XDP turn-on mode
   ```

    Finally verify that not eBPF map showed up via `sudo bpftool map show`.


### Expected Output

#### Test with `nc`
Try to generate some UDP/TCP traffic and watch for the userspace outputs. I have validated via the `nc` low speed approach:

1. On `nvidarm`, start a `nc` UDP server in keep listening mode: `nc -l -u -k <port_number>`;
2. On another node, send UDP traffic to `nvidarm`'s high speed Ethernet IP, 129.57.177.126, `nc -u 129.57.177.126 <port_number>`.

While sending these UDP traffic, you should be able to see the value printed to the screen changes, and the IP address match your test case.

```bash
Tracking per-IP TCP/UDP traffic:
...
IP: 129.57.178.31 - TCP Packets: 12, TCP Bytes: 720 | UDP Packets: 3, UDP Bytes: 120
IP: 129.57.178.31 - TCP Packets: 12, TCP Bytes: 720 | UDP Packets: 4, UDP Bytes: 153  # Recieved another tc UDP packet
```

#### Test with `iperf3`

See the guide in [iperf3.md](../../docs/iperf3).
