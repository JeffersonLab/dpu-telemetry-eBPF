#include <linux/bpf.h>  // needs -I/usr/include/aarch64-linux-gnu when compile & build
#include <linux/pkt_cls.h>  // #define TC_ACT_OK 0
#include <linux/if_ether.h>  // #define ETH_P_IP 0x0800
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/udp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>  // #define bpf_ntohs(x) 

#include "tc_common.h" // header file for this project only


struct {
    __uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
    /// NOTE: fixed number of entries will cause loss of statistics.
    __uint(max_entries, 2048);
    __type(key, struct traffic_key_t);
    __type(value, struct traffic_val_t);
} map_in_tc SEC(".maps");

/** Section to acctach to the TC ingress rule via: 
 * sudo tc qdisc add dev <net_iface> clsact
 * sudo tc filter add dev <net_iface> ingress bpf da obj <tc_kernel_obj>.o sec <sec_name>
 */
SEC("tc-ing")
int tc_egress(struct __sk_buff *skb) {
    void *data = (void *)(long)skb->data;  // start of the packet
    void *data_end = (void *)(long)skb->data_end;  // end of the packet

    // Memory overflow examination is a must-have to pass the eBPF program compiling.
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;

    if (eth->h_proto != bpf_htons(ETH_P_IP)) // Only process IPv4
        return TC_ACT_OK;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;

    // Track by source IP, network-order, big endian
    // The CPU is small endian.
    // To make it human readable, transfer to big endian (ntoh) in user space.
    struct traffic_key_t key = {
        .ip = ip->saddr,
        .proto = ip->protocol,
    };

    /// NOTE: Ignore 0.0.0.0 (0) and 255.255.255.255 (4294967295) for now.
    if (key.ip == 0)
        return TC_ACT_OK;

    // Only count the UDP and TCP traffix now.
    if (key.proto != IPPROTO_TCP && key.proto != IPPROTO_UDP)
        return TC_ACT_OK;

    struct traffic_val_t *val = bpf_map_lookup_elem(&map_in_tc, &key);
    if (!val) {
        // Create a new map entry. Fill the key not the value
        struct traffic_val_t zero = {};
        bpf_map_update_elem(&map_in_tc, &key, &zero, BPF_ANY);
        val = bpf_map_lookup_elem(&map_in_tc, &key);
        if (!val)
            return TC_ACT_OK;
    }

    // Update the Map's value field.
    val->packets += 1;
    val->bytes += bpf_ntohs(ip->tot_len);  // L3 and above length
        
    return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
