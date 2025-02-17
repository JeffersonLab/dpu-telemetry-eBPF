### Methods to configure, compile and run BPF applications

1. Install dependencies:
    - Clang and LLVM: `dnf/apt install -y clang llvm`.
    - eBPF tools: on RHEL, `dnf install -y bpf-tools bcc libbpf-devel`; on Ubuntu, `apt install -y libbpf-dev libelf-dev bcc`.
    - Linux kernel header files: `dnf/apt install -y kernel-headers-$(uname -r)`.
    - [Optional, on older Linux kernels] `dnf/apt install -y iproute2`.
2. Build and compile the code 
   - Generate "vmlinux.h"
  
     [On my RL8 laptop] `bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h`

   - Compile and build the program

3. Run the eBPF object


---
```bash
# Check the `bpf_printk()` output 
$ sudo cat /sys/kernel/debug/tracing/trace_pipe | grep bpf
    <idle>-0       [022] ..s.. 880819.722294: bpf_trace_printk: Got IP packet: [src IP: 2168009773], tot_len: 52, ttl: 59
    <idle>-0       [022] ..s.. 880819.724814: bpf_trace_printk: Got IP packet: [src IP: 2168009773], tot_len: 52, ttl: 59
# The above source IP 2168009773(u32) stands for 129.3.51.205
```
---
Follow [this doc](https://docs.google.com/document/d/1HD9Kl1NmDHEd3fA-lk71VtIeaRk823jnvbbTyF6ozoM/edit?tab=t.1mdhq355iate#heading=h.txbm8aif3pua) to see how I make the [tutorial](https://docs.google.com/document/d/1HD9Kl1NmDHEd3fA-lk71VtIeaRk823jnvbbTyF6ozoM/edit?tab=t.1mdhq355iate#heading=h.txbm8aif3pua) demo code working on my Linux kernel 4.8 laptop.

`sudo` is required for nearly every process.

A better README will be provided after I gain more knowledge.
