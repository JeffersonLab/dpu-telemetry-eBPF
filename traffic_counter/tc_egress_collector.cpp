/**
 * 
 * g++ -std=c++17 -O2 tc_egress_collector.cpp -o tc_egress_collector.o -lbpf -pthread
*/


#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <csignal>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cstring>

#include <netinet/in.h>  // For ntohl
#include <arpa/inet.h>   // For inet_ntop

#include "json.hpp"
#include "tc_common.h" // header file for this project only

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// ----- Configurable Parameters -----
int poll_hz = 50;
std::string map_path = "/sys/fs/bpf/tc-eg";
int export_interval = 1;
std::string output_path = "output-json.out";


struct PerExportBinsByIP {
    std::vector<__u64> tcp_bytes;
    std::vector<__u64> tcp_packets;
    std::vector<__u64> udp_bytes;
    std::vector<__u64> udp_packets;

    void ensure_capacity(size_t n) {
        if (tcp_bytes.size() < n) {
            tcp_bytes.resize(n);
            tcp_packets.resize(n);
            udp_bytes.resize(n);
            udp_packets.resize(n);
        }
    }
};

struct LastSeen {
    __u64 tcp_bytes = 0;
    __u64 tcp_packets = 0;
    __u64 udp_bytes = 0;
    __u64 udp_packets = 0;
};

// ----- Global Shared State -----
std::atomic<bool> running(true);
void handle_signal(int) {
    running = false;
}

std::shared_mutex data_mutex;
std::map<time_t, std::map<uint32_t, PerExportBinsByIP>> metric_bins;
bool first_report = true;

void print_latest_metric_bin() {
    std::unique_lock lock(data_mutex);

    if (metric_bins.empty()) {
        std::cout << "[metric_bins] is empty.\n";
        return;
    }

    const auto& [ts, ip_map] = *std::prev(metric_bins.end());  // last inserted timestamp
    std::cout << "Latest timestamp: " << ts << std::endl;

    for (const auto& [ip, bins] : ip_map) {
        std::cout << "  IP: " << ip << std::endl;

        auto print_vec = [](const std::string& label, const std::vector<__u64>& vec) {
            std::cout << "    " << label << " = [";
            for (size_t i = 0; i < vec.size(); ++i) {
                std::cout << vec[i];
                if (i != vec.size() - 1) std::cout << ", ";
            }
            std::cout << "]\n";
        };

        print_vec("tcp_bytes", bins.tcp_bytes);
        print_vec("tcp_packets", bins.tcp_packets);
        print_vec("udp_bytes", bins.udp_bytes);
        print_vec("udp_packets", bins.udp_packets);
    }

}


time_t now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now().time_since_epoch()
    ).count();
}

/**
 * @brief Compute the difference between consecutive values in a cumulative snapshot vector.
 *
 * This function generates a delta vector from a cumulative counter vector (`snapshot`),
 * where each element represents the change in value from the previous one.
 * The first delta is computed using the provided `last_seen` value, and all subsequent
 * deltas are computed as `snapshot[i] - snapshot[i - 1]`.
 *
 * The function does not modify the input `snapshot` vector or `last_seen`.
 *
 * @param snapshot   Vector of cumulative values (e.g., bytes or packets).
 * @param last_seen  Reference to the last value seen before this snapshot.
 *                   Used to compute the first delta.
 *
 * @return A vector of deltas representing changes between adjacent snapshot values.
 */
std::vector<__u64> get_diff_vector(const std::vector<__u64>& snapshot, const __u64& last_seen) {
    std::vector<__u64> diff;
    size_t n = snapshot.size();
    if (n == 0) return diff;

    diff.reserve(n);
    diff.push_back(snapshot[0] - last_seen);

    for (size_t i = 1; i < n; ++i) {
        /// TODO: --verbose mode
        // if (snapshot[i] < snapshot[i - 1]) {
        //     std::cout << "snapshot[i] < snapshot[i - 1]" << snapshot[i] << snapshot[i - 1] << i << std::endl;
        // }
        diff.push_back(snapshot[i] - snapshot[i - 1]);
    }

    return diff;
}


void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog <<\
        " [-p poll-hz] [-e export-interval][-m map-path] [-o output-path]" << std::endl;
}

void parse_args(int argc, char** argv,
    int& poll_hz, int & interval, std::string& map_path, std::string& output_path) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--poll-hz") && i + 1 < argc) {
            poll_hz = std::stoi(argv[++i]);
        } else if ((arg == "-e" || arg == "--export-interval") && i + 1 < argc) {
            export_interval = std::stoi(argv[++i]);
        } else if ((arg == "-m" || arg == "--map-path") && i + 1 < argc) {
            map_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output-path") && i + 1 < argc) {
            output_path = argv[++i];
        } else {
            print_usage(argv[0]);
            exit(1);
        }
    }
}

/**
 * @brief Take a snapshot of an eBPF LRU hash map and separate entries into TCP and UDP maps.
 *
 * If a protocol other than TCP or UDP is encountered, a warning is printed to `std::cerr`,
 * and the function returns -1 to indicate that some unexpected entries were found.
 *
 * @param map_fd        File descriptor of the BPF map (BPF_MAP_TYPE_LRU_HASH) to read from.
 * @param snapshot_tcp  Output map storing {IP -> traffic_val_t} entries for TCP traffic.
 * @param snapshot_udp  Output map storing {IP -> traffic_val_t} entries for UDP traffic.
 *
 * @return int  Returns 0 on success (only TCP/UDP entries encountered), or
 *              -1 if any unsupported protocol entries were found in the map.
 *
 * @note The IP address is stored in networking byte order (big-endian) in the output maps.
 */
int get_snapshot_bpf_map(int map_fd,
                     std::map<uint32_t, traffic_val_t>& snapshot_tcp,
                     std::map<uint32_t, traffic_val_t>& snapshot_udp) {
    traffic_key_t key{}, next_key{};
    traffic_val_t value{};
    bool has_unknown_proto = false;

    while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
        if (bpf_map_lookup_elem(map_fd, &next_key, &value) == 0) {
            if (next_key.proto == IPPROTO_TCP) {
                snapshot_tcp[next_key.ip] = value;
            } else if (next_key.proto == IPPROTO_UDP) {
                snapshot_udp[next_key.ip] = value;
            } else {
                has_unknown_proto = true;
                char ip_str[INET_ADDRSTRLEN];
                struct in_addr addr = { .s_addr = next_key.ip };
                inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
                std::cerr << "Warning: unsupported proto " << static_cast<int>(next_key.proto)
                          << " for IP " << ip_str << std::endl;
            }
        }
        key = next_key;
    }

    return has_unknown_proto ? -1 : 0;
}


/**
 * @brief Append traffic snapshot data into per-IP, per-second time bins.
 *
 * This function updates the global `metric_bins` structure by inserting the
 * current TCP and UDP traffic snapshot for each IP address at a specific time
 * bin and polling slot. The data is appended to vectors inside the 
 * `PerExportBinsByIP` struct, indexed by `polling_id`.
 *
 * If the vectors do not have enough capacity to accommodate `polling_id`,
 * they are resized appropriately using `ensure_capacity()`.
 * 
 * @param timestamp      The current second timestamp (e.g., from time(NULL)).
 * @param polling_id     The index within the current second, representing the
 *                       polling slot (typically from 0 to poll_hz - 1).
 * @param snapshot_tcp   Map of TCP traffic data, keyed by source IP address.
 * @param snapshot_udp   Map of UDP traffic data, keyed by source IP address.
 *
 * @note This function acquires a mutex lock internally to safely update
 *       the global `metric_bins` structure.
 */
void append_snapshot_to_metric_bins(
    time_t timestamp,
    int polling_id,
    const std::map<uint32_t, traffic_val_t>& snapshot_tcp,
    const std::map<uint32_t, traffic_val_t>& snapshot_udp) {

    std::unique_lock lock(data_mutex);  // Protect global access to metric_bins

    for (const auto& [ip, val] : snapshot_tcp) {
        auto& bin = metric_bins[timestamp][ip];
        bin.ensure_capacity(polling_id + 1);
        bin.tcp_bytes[polling_id]   += val.bytes;
        bin.tcp_packets[polling_id] += val.packets;
    }

    for (const auto& [ip, val] : snapshot_udp) {
        auto& bin = metric_bins[timestamp][ip];
        bin.ensure_capacity(polling_id + 1);
        bin.udp_bytes[polling_id]   += val.bytes;
        bin.udp_packets[polling_id] += val.packets;
    }
}


void print_in_json(
    const time_t ts, std::map<uint32_t, LastSeen>& last_seen) {
    json j_ts;

    for (const auto& [ip, bins] : metric_bins[ts]) {
        json j_ip;

        if (first_report) {
            last_seen[ip].tcp_bytes = bins.tcp_bytes.front();
            last_seen[ip].tcp_packets = bins.tcp_packets.front();
            last_seen[ip].udp_bytes = bins.udp_bytes.front();
            last_seen[ip].udp_packets = bins.udp_packets.front();
            first_report = false;
        }

        if (last_seen[ip].tcp_bytes != bins.tcp_bytes.back()) {
            j_ip["tcp_bytes"]   = get_diff_vector(bins.tcp_bytes,   last_seen[ip].tcp_bytes);
            j_ip["tcp_packets"] = get_diff_vector(bins.tcp_packets, last_seen[ip].tcp_packets);
            last_seen[ip].tcp_bytes = bins.tcp_bytes.back();
            last_seen[ip].tcp_packets = bins.tcp_packets.back();
        }
        if (last_seen[ip].udp_bytes != bins.udp_bytes.back()) {
            j_ip["udp_bytes"]   = get_diff_vector(bins.udp_bytes,   last_seen[ip].udp_bytes);
            j_ip["udp_packets"] = get_diff_vector(bins.udp_packets, last_seen[ip].udp_packets);
            last_seen[ip].udp_bytes = bins.udp_bytes.back();
            last_seen[ip].udp_packets = bins.udp_packets.back();
        }
        
        if (j_ip.empty())
            continue;

        /// TODO: update this to include a (src, dst) pair
        j_ts[std::to_string(ip)] = j_ip;
    }

    if (j_ts.empty())
        return;
    
    json record;
    record[std::to_string(ts)] = j_ts;

    /// TODO: not dump to screen

    std::cout << record.dump() << std::endl;
}


/**
 * @brief Continuously polls a BPF map and aggregates traffic snapshots into time-binned metrics.
 *
 * This function opens a BPF map specified by its filesystem path and enters a loop to
 * poll traffic statistics at a fixed frequency (`poll_hz`). It snapshots the map contents
 * at each polling interval and appends them to the global `metric_bins` structure,
 * using timestamp and polling slot index.
 *
 * Aggregated results are organized into per-IP, per-protocol vectors within each time bin.
 * After `poll_hz * export_interval` polls (i.e., one full export window), the polling counter resets.
 *
 * @param map_path        Path to the pinned BPF map in the BPF filesystem (e.g., /sys/fs/bpf/...).
 * @param poll_hz         Number of times to poll the map per second (polling frequency in Hz).
 * @param export_interval Number of seconds to accumulate before completing an export window.
 *
 * @note This function runs as long as the global `running` flag is true.
 *       The snapshot appending is thread-safe using internal locking.
 */
void poll_map_loop(const std::string& map_path, int poll_hz, int export_interval) {
    std::map<uint32_t, LastSeen> last_seen;

    int map_fd = bpf_obj_get(map_path.c_str());
    if (map_fd < 0) {
        perror("Failed to open BPF map");
        exit(1);
    }

    int interval_us = 1000000 / poll_hz;
    int bin_len = poll_hz * export_interval;

    time_t last_ts = now_sec();
    uint32_t polling_counter = 0;
    while (running) {
        std::map<u_int32_t, traffic_val_t> snapshot_tcp;
        std::map<u_int32_t, traffic_val_t> snapshot_udp;
        time_t now = now_sec();

        if (now != last_ts) {
            // print_latest_metric_bin();
            print_in_json(last_ts, last_seen);
            polling_counter = 0;
            last_ts = now;
        }
        
        if (get_snapshot_bpf_map(map_fd, snapshot_tcp, snapshot_udp) == 0) {
            append_snapshot_to_metric_bins(now, polling_counter, snapshot_tcp, snapshot_udp);
        }
        // std::cout << "now=" << now << ", polling_counter=" << polling_counter << std::endl;

        polling_counter += 1;
        usleep(interval_us);
    }
}


int main(int argc, char** argv) {
    parse_args(argc, argv, poll_hz, export_interval, map_path, output_path);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // std::thread poller([&]() {
    //     poll_map_loop(map_path, poll_hz, export_interval);
    // });

    poll_map_loop(map_path, poll_hz, export_interval);

    // poller.join();

    return 0;
}
