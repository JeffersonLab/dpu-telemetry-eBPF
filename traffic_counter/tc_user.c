#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <bpf/bpf.h>
#include <errno.h>
#include <string.h>

struct ip_proto_stats {
    __u64 tcp_bytes;
    __u64 tcp_packets;
    __u64 udp_bytes;
    __u64 udp_packets;
};

int main() {
    int map_fd;
    __u32 key, next_key;
    struct ip_proto_stats value;

    // Open the pinned map directly
    const char *map_path = "/sys/fs/bpf/ip_src_map";
    map_fd = bpf_obj_get(map_path);
    if (map_fd < 0) {
        perror("Failed to open pinned BPF map");
        return 1;
    }

    printf("Tracking per-IP TCP/UDP traffic:\n");

    while (1) {
        key = 0;
        while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
            if (bpf_map_lookup_elem(map_fd, &next_key, &value) == 0) {
                char ip_str[INET_ADDRSTRLEN];
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
