#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <net/if.h>

struct ip_proto_stats {
    __u64 tcp_bytes;
    __u64 tcp_packets;
    __u64 udp_bytes;
    __u64 udp_packets;
};

int main() {
    struct bpf_object *obj;
    int map_fd;
    __u32 key, next_key;
    struct ip_proto_stats value;

    // Load eBPF program. Name has to match our compiled eBPF object.
    obj = bpf_object__open_file("tc_traffic_count.o", NULL);
    if (!obj) {
        fprintf(stderr, "Failed to open eBPF object file\n");
        return 1;
    }

    // Load eBPF maps
    if (bpf_object__load(obj)) {
        fprintf(stderr, "Failed to load eBPF program\n");
        return 1;
    }

    // Match the map name we defined in the eBPF side.
    map_fd = bpf_object__find_map_fd_by_name(obj, "ip_proto_traffic_map");
    if (map_fd < 0) {
        fprintf(stderr, "Failed to find BPF map\n");
        return 1;
    }

    printf("Tracking per-IP TCP/UDP traffic:\n");

    while (1) {
        key = 0;
        while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
            if (bpf_map_lookup_elem(map_fd, &next_key, &value) == 0) {
                char ip_str[16];
                inet_ntop(AF_INET, &next_key, ip_str, sizeof(ip_str));
                printf("IP: %s - TCP Packets: %llu, TCP Bytes: %llu | UDP Packets: %llu, UDP Bytes: %llu\n",
                       ip_str, value.tcp_packets, value.tcp_bytes, value.udp_packets, value.udp_bytes);
            }
            key = next_key;
        }
        sleep(1);
    }

    return 0;
}
