#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

struct ip_proto_stats {
    __u64 tcp_bytes;
    __u64 tcp_packets;
    __u64 udp_bytes;
    __u64 udp_packets;
};

// LRU Hash Map for per-IP traffic statistics.
// Map name "ip_xxx_map" has to match the user space code.
struct {
    // We may need "__uint(type, BPF_MAP_TYPE_PERCPU_HASH)" for high speed case
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024); // Track up to 1024 IPs
    __type(key, __u32);  // IPv4 Address
    __type(value, struct ip_proto_stats);
} ip_proto_traffic_map SEC(".maps");

SEC("classifier")
int count_ip_proto_traffic(struct __sk_buff *skb) {
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;

    if (eth->h_proto != __constant_htons(ETH_P_IP)) // Only process IPv4
        return TC_ACT_OK;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;

    __u32 ip_key = ip->saddr; // Track by source IP

    struct ip_proto_stats *stats = bpf_map_lookup_elem(&ip_proto_traffic_map, &ip_key);
    if (!stats) {
        struct ip_proto_stats new_stats = {};
        bpf_map_update_elem(&ip_proto_traffic_map, &ip_key, &new_stats, BPF_ANY);
        stats = bpf_map_lookup_elem(&ip_proto_traffic_map, &ip_key);
        if (!stats)
            return TC_ACT_OK;
    }

    // payload_len = IP header + transport header + payload
    __u16 payload_len = bpf_ntohs(ip->tot_len);
    // For BW estimation, use the packet length:
    // Ethernet header + IP header + transport header + payload
    // pkt_size = data_end - data;
    
    if (ip->protocol == IPPROTO_TCP) { // TCP
        __sync_fetch_and_add(&stats->tcp_packets, 1);
        __sync_fetch_and_add(&stats->tcp_bytes, payload_len);
    } else if (ip->protocol == IPPROTO_UDP) { // UDP
        __sync_fetch_and_add(&stats->udp_packets, 1);
        __sync_fetch_and_add(&stats->udp_bytes, payload_len);
    }

    return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
