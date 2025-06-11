/**
 * The TC kernel program to count the outgoing IPv4 TCP/UDP packets.
 * 
 * 
 * Checked-in date: June 10, 2025
 * Author: xmei@jlab.org, ChatGPT
 * Test: "nvidarm" Host
 */

#include <linux/bpf.h>
#include <linux/pkt_cls.h>  // #define TC_ACT_OK 0
#include <linux/if_ether.h>  // #define ETH_P_IP 0x0800
#include <linux/ip.h>
#include <linux/in.h>  // For IPPROTO_TCP etc.
#include <linux/udp.h>
#include <linux/tcp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>  // #define bpf_ntohs(x)

#include "tc_common.h" // header file for this project only

/// TODO: check if PERCORE eBPF Map is needed to meet the high speed traffic needs.
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    /// TODO: fixed number of entries will cause loss of statistics.
    __uint(max_entries, 2048);
    __type(key, struct traffic_key_t);
    __type(value, struct traffic_val_t);
} map_out_tc SEC(".maps");

/** Section to acctach to the TC egress rule via: 
 * sudo tc qdisc add dev <net_iface> clsact
 * sudo tc filter add dev <net_iface> egress bpf da obj <tc_kernel_obj>.o sec <sec_name>
 */
SEC("tc-eg")
int tc_egress(struct __sk_buff *skb) {
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    // Parse Ethernet header. Only process IPv4.
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;

    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return TC_ACT_OK;

    // Parse IP header
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;

    struct traffic_key_t key = {
        .ip = ip->daddr,
        .proto = ip->protocol,
    };

    if (key.ip == 4294967295)  // NOTE: ignore 255.255.255 for now
        return TC_ACT_OK;

    // Only count the UDP and TCP traffix now.
    if (key.proto != IPPROTO_TCP && key.proto != IPPROTO_UDP)
    return TC_ACT_OK;

    struct traffic_val_t *val = bpf_map_lookup_elem(&map_out_tc, &key);
    if (!val) {
        struct traffic_val_t zero = {};
        bpf_map_update_elem(&map_out_tc, &key, &zero, BPF_ANY);
        val = bpf_map_lookup_elem(&map_out_tc, &key);
        if (!val)
            return TC_ACT_OK;
    }

    __sync_fetch_and_add(&val->packets, 1);
    __sync_fetch_and_add(&val->bytes, bpf_ntohs(ip->tot_len));

    return TC_ACT_OK;
}
