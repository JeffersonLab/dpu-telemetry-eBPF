#include "vmlinux.h"

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define TC_ACT_OK 0
#define ETH_P_IP 0x0800 /* Internet Protocol packet */

/* Below after "///" are not comments. They are eBPF definiations. */

/// @tchook {"ifindex":1, "attach_point":"BPF_TC_INGRESS"}
/// @tcopts {"handle":1, "priority":1}
SEC("tc")
int tc_ingress(struct __sk_buff *ctx)
{
    void *data_end = (void *)(__u64)ctx->data_end;
    void *data = (void *)(__u64)ctx->data;
    struct ethhdr *l2;
    struct iphdr *l3;

    if (ctx->protocol != bpf_htons(ETH_P_IP))
        return TC_ACT_OK;

    l2 = data;
    if ((void *)(l2 + 1) > data_end)
        return TC_ACT_OK;  // ensure packet is within bound

    l3 = (struct iphdr *)(l2 + 1);
    if ((void *)(l3 + 1) > data_end)
        return TC_ACT_OK;

    // Save Src IP and print (in u32. Processing this u32 to 4 bytes will cause error)
    __u32 src_ip = bpf_ntohl(l3->saddr);

    bpf_printk("Got IP packet: [src IP: %u], tot_len: %d, ttl: %d",
        src_ip,
        bpf_ntohs(l3->tot_len), l3->ttl);

    return TC_ACT_OK;
}

char __license[] SEC("license") = "GPL";
