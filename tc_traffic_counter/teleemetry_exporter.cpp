
/*
Try to delete the cold hash keys every 5s and offload the values every 10 ms.

Compile with:
g++ -std=c++17 -o telemetry_reader telemetry_reader.cpp -lbpf

It's not tested and mainly by ChatGPT.

*/

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unordered_map>

// Replace with your actual struct
struct ip_proto_stats {
    __u64 tcp_packets;
    __u64 tcp_bytes;
    __u64 udp_packets;
    __u64 udp_bytes;
};

// User-space cold key tracking (you can optimize this)
std::unordered_map<__u32, uint64_t> last_seen_ns;

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec);
}

int main() {
    int map_fd = bpf_obj_get("/sys/fs/bpf/ip_src_map");
    int num_cpus = libbpf_num_possible_cpus();
    struct ip_proto_stats *values = calloc(num_cpus, sizeof(*values));
    struct ip_proto_stats *zero = calloc(num_cpus, sizeof(*zero));

    __u32 key = 0, next_key;
    uint64_t last_cleanup = now_ns();

    while (1) {
        key = 0;
        uint64_t now = now_ns();

        while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
            struct ip_proto_stats agg = {0};
            if (bpf_map_lookup_elem(map_fd, &next_key, values) == 0) {
                for (int i = 0; i < num_cpus; i++) {
                    agg.tcp_packets += values[i].tcp_packets;
                    agg.tcp_bytes   += values[i].tcp_bytes;
                    agg.udp_packets += values[i].udp_packets;
                    agg.udp_bytes   += values[i].udp_bytes;
                }

                // Offload the telemetry (e.g., log / push to InfluxDB / Prometheus)
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &next_key, ip_str, sizeof(ip_str));
                printf("IP %s | TCP pkts: %llu, bytes: %llu | UDP pkts: %llu, bytes: %llu\n",
                       ip_str, agg.tcp_packets, agg.tcp_bytes, agg.udp_packets, agg.udp_bytes);

                // Reset value to 0 (fast)
                bpf_map_update_elem(map_fd, &next_key, zero, BPF_EXIST);

                // Track last seen time for this key
                last_seen_ns[next_key] = now;
            }
            key = next_key;
        }

        // Every 5 seconds: delete cold keys
        if ((now - last_cleanup) >= 5 * 1000000000ull) {
            for (auto it = last_seen_ns.begin(); it != last_seen_ns.end(); ) {
                if ((now - it->second) > 5 * 1000000000ull) {
                    bpf_map_delete_elem(map_fd, &it->first);
                    it = last_seen_ns.erase(it);
                } else {
                    ++it;
                }
            }
            last_cleanup = now;
        }

        // Sleep 10ms
        usleep(10000);
    }

    free(values);
    free(zero);
    return 0;
}
