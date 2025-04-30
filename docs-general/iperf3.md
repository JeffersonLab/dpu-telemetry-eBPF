## High-Performance `iperf3` Installation and Bandwidth Testing Guide

This guide walks through building ESnet’s [`iperf3`](https://github.com/esnet/iperf) from source and performing high-throughput bandwidth tests using modern network interfaces.

---

### 🛠️ Build and Install ESnet’s `iperf3`

1. Install Dependencies
Ensure development tools are available:

    ```bash
    sudo apt install git gcc make autoconf automake libtool -y   # Debian/Ubuntu
    # or for Fedora/RHEL-based:
    # sudo dnf install git gcc make autoconf automake libtool -y
    ```
2. Clone the ESnet Repository
    ```bash
    git clone https://github.com/esnet/iperf.git
    cd iperf
    ```
3. Build and Install to /usr/local
    ```bash
    ./configure --prefix=/usr/local
    make
    sudo make install
    ```

4. Add `/usr/local/bin` to your `PATH` (if needed). Add `/usr/local/lib` to `LD_LIBRARY_PATH`.
    ```bash
    echo 'export PATH=/usr/local/bin:$PATH' >> ~/.bashrc
    echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
    source ~/.bashrc
    iperf3 --version   # verify
    ```


### Bandwidth Testing
1. Server (on destination node). 
   
   For `nvidarm` host, choose the high-speed Ethernet/DPU's IPv4 address, 129.57.177.126.
    ```bash
    xmei@nvidarm:~/iperf$ iperf3 -s -B 129.57.177.126
    ----------------------------------------------------
    Server listening on 5201 (test #1)
    ----------------------------------------------------
    ```
2. Client (on source node).
   
    ```bash
    iperf3 -c <server_ip> -B <client_ip> -t 60 -P 8
    # -B <client_ip>: Bind to interface IP (e.g., 129.57.177.6 for ejfat6's high speed Ethernet)
    # -t 60: Duration in seconds
    # -P 8: 8 parallel TCP streams to maximize throughput
    ```

Using the above configuration, the TCP `iperf` traffic from `ejfat-6` to `nvidarm` can achieve 98.4 Gbps.

```bash
# Report on client side
[SUM]   0.00-60.00  sec   688 GBytes  98.5 Gbits/sec   48             sender  # 48 refers to total retransmission packets
[SUM]   0.00-60.00  sec   688 GBytes  98.4 Gbits/sec                  receiver

# 688 GB = 688 * 2^{30} = 739,145,227,776 Bytes
# The ebpf counter counts 739151234907 bytes, which is super close.
IP: 129.57.177.6 - TCP Packets: 14153635, TCP Bytes: 739151234907 | UDP Packets: 0, UDP Bytes: 0   
```

### UDP Test at Line Rate
```bash
iperf3 -c <server_ip> -B <client_ip> -u -b <Gbps>G -t 60 -P 4 -l 8948
# -u: Use UDP
# -b: Target bandwidth in bitrate
# -l 8948: the length of each packet when MTU=9000
```

Single stream performance (without option `-P x`, with option `-b 100G`)
```
[ ID] Interval           Transfer     Bitrate         Jitter    Lost/Total Datagrams
[  5]   0.00-60.00  sec   193 GBytes  27.7 Gbits/sec  0.000 ms  0/23195918 (0%)  sender
[  5]   0.00-60.00  sec   141 GBytes  20.1 Gbits/sec  0.004 ms  6312594/23195918 (27%)  receiver
```

Reported maximum UDP performance is 97.1 Gbps with 25 parallel sending streams. The performance is not stable.
```bash
# iperf3 -c 129.57.177.126 -B 129.57.177.6 -t 20 -u -l 8948 -P 25 -b 4G
[SUM]   0.00-20.01  sec   233 GBytes   100 Gbits/sec  0.000 ms  0/27952582 (0%)  sender
[SUM]   0.00-20.01  sec   226 GBytes  97.1 Gbits/sec  0.017 ms  797853/27950551 (2.9%)  receiver
```

### Advanced Optimization Tips

```bash
sudo ip link set dev enp193s0f1np1 mtu 9000  # Set NIC to use jumbo frames:
sudo ethtool -K enp193s0f1np1 gro off gso off tso off  # Disable offloads (optional for benchmarking)
sudo cpupower frequency-set -g performance  # Ensure CPU governor is set to performance
taskset -c 2-3 iperf3 ...  # Bind interrupts and processes to specific cores (advanced)
iftop -i <dev_name>  # Monitoring realtime ip interface usage
```

Tune the sender and receiver buffer size.

```bash
sysctl net.core.rmem_max    # check the maximum receive buffer size
sysctl net.core.rmem_default  # check the default receive buffer size
sysctl net.core.wmem_max   # check the maximum send buffer size
sysctl net.core.wmem_default   # check the default send buffer size
# Set the buffer size to 128 MB
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w net.core.wmem_default=134217728
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.rmem_default=134217728
```
