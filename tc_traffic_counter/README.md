## Traffic counter by IPv4 addresses



```bash
sudo apt install linux-source
```

### Compile and run method
The below process is test and verified on "nvidarm" with the DPU Ethernet address 129.57.177.126.

1. Compile the eBPF Program
    ```bash
    sudo clang -O2 -g -target bpf -I/usr/include/aarch64-linux-gnu -c tc_kernel_ip_counter.c -o tc_ip_counter.o  # "-g" is required
    # tc_ip_counter.o is the one to attach
    ```
2. Attach this compiled eBPF program `tc_traffic_count.o` with `tc`.
   - Add clsact qdisc to the Ethernet device `enP2s1f0np0`: `sudo tc qdisc add dev enP2s1f0np0 clsact`.
   - Attach the program: `sudo tc filter add dev enP2s1f0np0 ingress bpf da obj tc_ip_counter.o sec classifier/ingress`. Needs to match kernel code "SEC" information.
   - (Optional) Inspect the eBPF map without a user space program.
  
        ```bash
        $ sudo bpftool map  # Find the map
        6: lru_hash  name ip_src_map  flags 0x0
        key 4B  value 32B  max_entries 1024  memlock 40960B
        btf_id 150
        # 6 is the map id; "ip_src_map" is the name which should match our definition in the C eBPF kernel code
        # If the ebpf map name is truncated and use the truncated one 
        $ sudo bpftool map dump name ip_src_map   # dump the map context
        $ sudo bpftool map dump id 6 # dump by id
        # Output example
        [{
                "key": 531773825,  # need ntoh transfer to see the IP
                "value": {
                    "tcp_bytes": 0,
                    "tcp_packets": 0,
                    "udp_bytes": 63,
                    "udp_packets": 2
                }
            }
        ]
        ```
3. **PIN** the map for the user space code: `sudo bpftool map pin name ip_src_map /sys/fs/bpf/ip_src_map`. Can either pin by name or id. Dump this map by pinned address: `sudo bpftool map dump pin /sys/fs/bpf/ip_src_map`. If you skip this step, you might end up open 2 eBPF map instances when you run the userspace code and never get any traffic stats for your userspace one.

4. Compile the userspace program: `gcc -o tc_user.o tc_user.c -lbpf`
5. Run the userspace program: `sudo ./tc_user.o`. The expected output is shown in the next section.

6. **UNPIN** the map and **CLEAN UP** the rules.
    ```bash
    # Pinning the map make it persistent and you can not deattach it.
    $ sudo rm /sys/fs/bpf/ip_src_map  # delete the pinned map
    $ sudo tc filter del dev enP2s1f0np0 ingress
    $ sudo tc qdisc del dev enP2s1f0np0 clsact
    $ sudo bpftool map  # verify no map showed up
    ```

### Expected Output

#### Test with `nc`
Try to generate some UDP/TCP traffic. I have tried the low speed validation via the `nc` approach:

1. On `nvidarm`, start a nc UDP server in keep listening mode: `nc -l -u -k <port_number>`;
2. On another node, send UDP traffic to `nvidarm`'s high speed Ethernet IP, 129.57.177.126, `nc -u 129.57.177.126 <port_number>`.

While sending these UDP traffic, you should be able to see the value printed to the screen changes, and the IP address match your test case.

```bash
Tracking per-IP TCP/UDP traffic:
...
P: 129.57.178.31 - TCP Packets: 12, TCP Bytes: 720 | UDP Packets: 3, UDP Bytes: 120
IP: 129.57.178.31 - TCP Packets: 12, TCP Bytes: 720 | UDP Packets: 4, UDP Bytes: 153
IP: 129.57.178.31 - TCP Packets: 12, TCP Bytes: 720 | UDP Packets: 4, UDP Bytes: 153
```

#### Test with `iperf3`

See the guide in [iperf3.md](../docs-general/iperf3.md).
