/**
 * The XDP kernel program to count the incoming IPv4 TCP/UDP packets.
 * 
 * NOTE: XDP driver/native mode ONLY applies to MTU <= 3498. If using jumbo frames,
 * such as MTU=9000, the program falls back to the XDP generic mode, which is slower
 * than TC.
 * 
 * Checked-in date: June 9, 2025
 * Author: xmei@jlab.org, ChatGPT
 * Test: "nvidarm" Host
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
// TODO: check if PERCORE eBPF Map is needed to meet the high speed traffic needs.
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 2048);   // TODO: fixed number of entries will cause loss of statistics.
    __type(key, struct traffic_key_t);
    __type(value, struct traffic_val_t);
} map_in_xdp SEC(".maps");  // "map_in_xdp" will the map name to be attached to network devices


/* Section to attach via `ip -force link set dev <net_iface> xdp obj <xdp_kernel_obj>.o sec <sec_name>` */
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
        .ip = ip->saddr,
        .proto = ip->protocol,
    };

    // Ignore the trffic from 0.0.0.0 now. Multicast or broadcast traffic?
    if (key.ip == 0)
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
    __u16 payload_len = bpf_ntohs(ip->tot_len);
    __sync_fetch_and_add(&val->packets, 1);
    __sync_fetch_and_add(&val->bytes, payload_len);

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";  // must-have
