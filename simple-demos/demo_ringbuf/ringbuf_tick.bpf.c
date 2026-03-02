#include <linux/types.h>   // for __u32/__u64

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>


struct tick_evt {
    __u64 ts_ns;
    __u64 seq_number;   // a number keep increasing
};


struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);   // 1 MiB, check whether space is enough
} rb SEC(".maps");


// For ringbuf reserve failure counts
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} drops SEC(".maps");


SEC("perf_event")
int on_tick(struct bpf_perf_event_data *ctx) {
    static __u64 seq;
    struct tick_evt *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        __u32 k = 0;
        __u64 *d = bpf_map_lookup_elem(&drops, &k);
        if (d) __sync_fetch_and_add(d, 1);
        return 0;
    }

    e->ts_ns = bpf_ktime_get_ns();
    e->seq_number = __sync_fetch_and_add(&seq, 1);

    // push-like kernel function
    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
