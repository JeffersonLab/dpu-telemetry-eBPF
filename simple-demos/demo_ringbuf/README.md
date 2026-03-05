
### Application: `ringbuf_tick`


`ringbuf_tick` is a minimal eBPF + userspace test program designed to measure how many events per second userspace can consume from a **BPF ring buffer**. It uses a **perf event–driven eBPF program** as a periodic kernel timer and pushes timestamped events into a ring buffer.


The system consists of two components:

* **Kernel eBPF program (`ringbuf_tick.bpf.c`)**

  * Attached to a `perf_event`
  * Executes at a configurable frequency (`--perf-freq`)
  * Generates a `tick_evt` containing a timestamp and sequence number
  * Writes the event to a `BPF_MAP_TYPE_RINGBUF`
  * Counts ring buffer reservation failures (`drops`)

* **Userspace program (`ringbuf_tick_user.c`)**

  * Attaches the eBPF program to a perf event on a chosen CPU
  * Polls the ring buffer using `ring_buffer__poll()`
  * Groups events into time bins (`--bin-freq`)
  * Reports runtime statistics every second

Every second the userspace program prints:
* **events/s** – number of events received from the ring buffer
* **bins/s** – number of time bins advanced (based on `--bin-freq`)
* **poll_calls/s** – number of `ring_buffer__poll()` calls
* **drops/s** – ring buffer reservation failures in the kernel
* **max_jitter_ns** – maximum deviation from the expected perf-event interval

#### Usage

```bash
sudo ./ringbuf_tick_user --perf-freq 4000 --bin-freq 4000
```

Parameters:

* `--perf-freq`
  Frequency (Hz) at which the kernel eBPF program is triggered via `perf_event`.

* `--bin-freq`
  Userspace binning frequency used to group events by timestamp.



#### Dump `BPF_MAP_TYPE_RINGBUF` and `BPF_MAP_TYPE_ARRAY`
```bash
[xmei@ejfat-6 telemetry-eBPF]$ sudo bpftool map show
[sudo] password for xmei: 
28: array  name seq  flags 0x0
        key 4B  value 8B  max_entries 1  memlock 272B
        btf_id 233
        pids ringbuf_tick.o(1288286)
29: ringbuf  name rb  flags 0x0
        key 0B  value 0B  max_entries 1048576  memlock 1065240B
        pids ringbuf_tick.o(1288286)
30: array  name drops  flags 0x0
        key 4B  value 8B  max_entries 1  memlock 272B
        btf_id 233
        pids ringbuf_tick.o(1288286)
# Dump the ringbbuf
[xmei@ejfat-6 telemetry-eBPF]$ sudo bpftool map dump id 29
Found 0 elements
# Dump the `BPF_MAP_TYPE_ARRAY`
[xmei@ejfat-6 telemetry-eBPF]$ sudo bpftool map dump id 28
[{
        "key": 0,
        "value": 802781
    }
]
```

#### Demo output
```bash
[xmei@ejfat-6 demo_ringbuf]$ sudo ./ringbuf_tick.o --perf-freq 50000 --bin-freq 100 --cpu 2
[sudo] password for xmei: 
Attached on CPU2: perf_freq=50000 Hz (target=20000 ns), bin_freq=100 Hz (bin=10000000 ns). Ctrl-C to stop.
[1772694385] events/s=47  bins/s=0  poll_calls/s=543192  drops/s=0  max_jitter_ns=5420
...
[1772694389] events/s=21999  bins/s=422  poll_calls/s=2338347  drops/s=0  max_jitter_ns=3781237811
[1772694390] events/s=50000  bins/s=100  poll_calls/s=2173474  drops/s=0  max_jitter_ns=3300
[1772694391] events/s=50000  bins/s=100  poll_calls/s=2176233  drops/s=0  max_jitter_ns=6180
[1772694392] events/s=49973  bins/s=100  poll_calls/s=2167490  drops/s=0  max_jitter_ns=195655
[1772694393] events/s=49938  bins/s=100  poll_calls/s=2179757  drops/s=0  max_jitter_ns=194244
[1772694394] events/s=49991  bins/s=100  poll_calls/s=2166328  drops/s=0  max_jitter_ns=185434
[1772694395] events/s=50000  bins/s=100  poll_calls/s=2176804  drops/s=0  max_jitter_ns=8400
[1772694396] events/s=49982  bins/s=100  poll_calls/s=2171396  drops/s=0  max_jitter_ns=183684
...
```

### Application: `ringbuf_basic`
- Userspace: 
    - eBPF kernel helpers: load the kernel object, find the eBPF map, attach the kernel object, .... This part can be done via command lines.
    - The `ring-buffer` handler: `handle_sample` to print the values out.
- Kernel: `submit` an event triggered by every `getpid` syscall.

```bash
[xmei@ejfat-6 demo_ringbuf]$ make run
cc -O2 -g -Wall -Wextra ringbuf_basic.c -o ringbuf_basic $(pkg-config --cflags --libs libbpf)
sudo ./ringbuf_basic ./ringbuf_basic.bpf.o
[sudo] password for xmei: 
Attached. Run `getpid()` by doing anything (e.g., `echo hi`) and watch events. Ctrl-C to stop.
...
ringbuf evt: tgid=1265243 pid=1265243 uid=11066 gid=761 ts=2896822836997609
...
ringbuf evt: tgid=3109 pid=105968 uid=984 gid=669 ts=2896822850642192
...
ringbuf evt: tgid=1265243 pid=1265243 uid=11066 gid=761 ts=2896822993483385
ringbuf evt: tgid=1265243 pid=1265243 uid=11066 gid=761 ts=2896823001109907
...
^C
```
