
### Application: `ringbuf_tick`
A benchmarking application to see how many ring-buffer events 

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
