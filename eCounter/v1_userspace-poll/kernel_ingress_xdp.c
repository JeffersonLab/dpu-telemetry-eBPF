/**
 * Checked-in date: June 9, 2025
 * Last updated: Sep 16, 2026
 *
 * The XDP kernel program to count the incoming IPv4 TCP/UDP blocks.
 *
 * NOTE: XDP driver/native mode ONLY applies to MTU <= 3498. If using jumbo frames,
 * such as MTU=9000, the program falls back to the XDP generic mode, which is slower
 * than TC.
 */


#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "tc_common.h"

// If the map name ("map_in_xdp" here) is too long, it will be truncated.
/// TODO: check if PERCORE eBPF Map is needed to meet the high speed traffic needs.
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    /// TODO: fixed number of entries will cause loss of statistics.
    __uint(max_entries, 2048);
    __type(key, struct traffic_key_t);
    __type(value, struct traffic_val_t);
} map_in_xdp SEC(".maps");  // "map_in_xdp" will the map name to be attached to network devices


/* Section to attach via `ip link set dev <net_iface> xdp obj <xdp_kernel_obj>.o sec <sec_name>` */
SEC("xdp-ing")
int xdp_ingress(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // Memory overflow examination is a must-have to pass the eBPF program compiling.
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;  // return values differ from those of TC programs

    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    struct traffic_key_t key = {
        .source_ip = ip->saddr,
        .destination_ip = ip->daddr,
        .proto = ip->protocol,
    };

    /// NOTE: Ignore blocks from 0.0.0.0, the unspecified address. Only a host
    /// without an IP yet sends from it; for IPv4 TCP/UDP that is DHCP
    /// (DISCOVER/REQUEST, 0.0.0.0:68 -> 255.255.255.255:67). Broadcast and
    /// multicast are destination addresses and are NOT filtered here.
    if (key.source_ip == 0)
        return XDP_PASS;
    
    // Only count the UDP and TCP traffix now.
    if (key.proto != IPPROTO_TCP && key.proto != IPPROTO_UDP)
        return XDP_PASS;

    struct traffic_val_t *val = bpf_map_lookup_elem(&map_in_xdp, &key);
    if (!val) {
        // Create a new map entry. Fill the key not the value
        struct traffic_val_t zero = {};
        bpf_map_update_elem(&map_in_xdp, &key, &zero, BPF_ANY);
        val = bpf_map_lookup_elem(&map_in_xdp, &key);
        if (!val)
            return XDP_PASS;
    }

    // Update the Map's value field.
    // XDP runs in the driver before the kernel allocates an skb, so skb->len
    // (used by the TC programs) is not available here. For now count L3 and
    // above only, excluding the Ethernet header.
    __u16 payload_len = bpf_ntohs(ip->tot_len);  // L3 and above length
    __sync_fetch_and_add(&val->blocks, 1);
    __sync_fetch_and_add(&val->bytes, payload_len);

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";  // must-have
