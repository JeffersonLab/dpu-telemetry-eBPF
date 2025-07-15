#include <bpf/libbpf.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// ---- Map definition (adjust as needed) ----
struct ip_dir_key {
    __u32 src_ip;
    __u32 dst_ip;
    __u8 dir;  // ingress/egress
};

struct ip_proto_stats {
    __u64 packets;
    __u64 bytes;
};

// ---- Globals ----
std::atomic<bool> running(true);
std::shared_mutex data_mutex;
std::map<time_t, std::map<ip_dir_key, ip_proto_stats>> second_bins;

// ---- Configurable parameters ----
constexpr int n = 100;  // e.g., 100Hz polling
constexpr char MAP_PATH[] = "/sys/fs/bpf/tc-eg";  // Adjust as needed

// ---- Helpers ----
time_t now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now().time_since_epoch()).count();
}

void poll_map_loop() {
    bpf_map* map = bpf_obj_get(MAP_PATH);
    if (map < 0) {
        perror("Failed to open BPF map");
        exit(1);
    }

    int interval_us = 1'000'000 / n;

    while (running) {
        auto now = now_sec();

        ip_dir_key key{}, next_key{};
        ip_proto_stats value{};
        std::map<ip_dir_key, ip_proto_stats> current_snapshot;

        while (bpf_map_get_next_key(map, &key, &next_key) == 0) {
            if (bpf_map_lookup_elem(map, &next_key, &value) == 0) {
                current_snapshot[next_key] = value;
            }
            key = next_key;
        }

        {
            std::unique_lock lock(data_mutex);
            for (auto& [k, v] : current_snapshot) {
                auto& bin = second_bins[now][k];
                bin.packets += v.packets;
                bin.bytes += v.bytes;
            }
        }

        usleep(interval_us);
    }
}

void export_loop() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(60));

        std::map<time_t, json> export_data;

        {
            std::unique_lock lock(data_mutex);
            for (auto& [ts, stats] : second_bins) {
                json j_entry;
                for (auto& [key, value] : stats) {
                    std::ostringstream key_str;
                    key_str << key.src_ip << "-" << key.dst_ip << "-" << (int)key.dir;
                    j_entry[key_str.str()] = {
                        {"packets", value.packets},
                        {"bytes", value.bytes}
                    };
                }
                export_data[ts] = j_entry;
            }
            second_bins.clear();  // Clear after export
        }

        // Write to file (in-memory could mean tmpfs or stringstream)
        std::ofstream out("output.json");
        out << std::setw(2) << export_data << std::endl;
    }
}

int main() {
    std::thread poller(poll_map_loop);
    std::thread exporter(export_loop);

    // Wait or catch signal
    signal(SIGINT, [](int) { running = false; });

    poller.join();
    exporter.join();

    return 0;
}
