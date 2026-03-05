// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* Must match userspace */
struct tick_evt {
    __u64 ts_ns;
    __u64 seq_number;   // monotonically increasing
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20); // 1 MiB
} rb SEC(".maps");


/* drops[0] = number of ringbuf reserve failures */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} drops SEC(".maps");


/* Global counter keep adding 1 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} seq SEC(".maps");


/* Perf-event program name MUST match userspace lookup: "on_tick" */
SEC("perf_event")
int on_tick(struct bpf_perf_event_data *ctx) {
    (void)ctx;

    __u32 key = 0;
    __u64 *pseq = bpf_map_lookup_elem(&seq, &key);
    __u64 s = 0;
    if (pseq) {
        s = *pseq + 1;
        *pseq = s;
    } else {
        /* extremely unlikely; but keep s = 0 */
    }

    struct tick_evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        __u64 *pd = bpf_map_lookup_elem(&drops, &key);
        if (pd)
            __sync_fetch_and_add(pd, 1);
        return 0;
    }

    e->ts_ns = bpf_ktime_get_ns();
    e->seq_number = s;

    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
