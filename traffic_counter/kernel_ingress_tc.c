#include <linux/bpf.h>  // needs -I/usr/include/aarch64-linux-gnu when compile & build
#include <linux/pkt_cls.h>  // #define TC_ACT_OK 0
#include <linux/if_ether.h>  // #define ETH_P_IP 0x0800
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/udp.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>  // #define bpf_ntohs(x) 


struct ip_proto_stats {
    __u64 tcp_bytes;
    __u64 tcp_packets;
    __u64 udp_bytes;
    __u64 udp_packets;
};

// LRU Hash Map for per-IP traffic statistics.
// Map name "ip_xxx_map" has to match the user space code.
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    // TODO: needs to be updated in production.
    __uint(max_entries, 1024); // Track up to 1024 IPs. 
    __type(key, __u32);  // IPv4 Address
    __type(value, struct ip_proto_stats);
} ip_src_map SEC(".maps");

SEC("classifier/ingress")
int src_ip_counter(struct __sk_buff *skb) {
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
    __u32 ip_key = ip->saddr;
    if (ip_key == 0)  // ignore 0.0.0.0 for whatever cause it.
        return TC_ACT_OK;

    __u16 payload_len = bpf_ntohs(ip->tot_len);

    // Only look into TCP & UDP traffic
    if (ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP) {
        struct ip_proto_stats *stats = bpf_map_lookup_elem(&ip_src_map, &ip_key);
        if (!stats) {
            struct ip_proto_stats new_stats = {};
            bpf_map_update_elem(&ip_src_map, &ip_key, &new_stats, BPF_ANY);
            stats = bpf_map_lookup_elem(&ip_src_map, &ip_key);
            if (!stats)
                return TC_ACT_OK;
        }
        
        if (ip->protocol == IPPROTO_TCP) {
            __sync_fetch_and_add(&stats->tcp_packets, 1);
            __sync_fetch_and_add(&stats->tcp_bytes, payload_len);
        } else {
            __sync_fetch_and_add(&stats->udp_packets, 1);
            __sync_fetch_and_add(&stats->udp_bytes, payload_len);
        }
    }
        
    return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
