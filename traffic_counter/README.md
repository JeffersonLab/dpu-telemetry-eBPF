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

See the guide in [iperf3.md](../docs/iperf3.md).
