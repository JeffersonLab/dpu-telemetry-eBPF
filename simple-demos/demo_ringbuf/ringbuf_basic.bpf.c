// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>   // for __u32/__u64

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct evt {
    __u64 ts_ns;
    __u32 pid;
    __u32 tgid;
    __u32 uid;
    __u32 gid;
};


struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);  // 1 MiB entries
} rb SEC(".maps");


SEC("tp/syscalls/sys_enter_getpid")
int handle_kernel_func(void *ctx) {
/// NOTE: this function is triggered only when there is a sys_enter_getpid syscall

    struct evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u64 uid_gid = bpf_get_current_uid_gid();

    e->ts_ns = bpf_ktime_get_ns();
    e->pid  = (__u32)pid_tgid;
    e->tgid = (__u32)(pid_tgid >> 32);
    e->uid  = (__u32)uid_gid;
    e->gid  = (__u32)(uid_gid >> 32);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
