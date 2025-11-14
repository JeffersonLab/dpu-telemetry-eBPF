/**
 * Collect local network traffic by src/dst IPv4 addresses at up to 1000 Hz per second.
 * Output the report in a JSON format.
 * 
 * Need to pin the eBPF map first. By default pinned to "/sys/fs/bpf/tc-eg".
 * 
 * Compile without CMakeLists.txt:
 *   g++ -std=c++17 -O2 <this-file>.cpp -o <this-file>.o -lbpf -pthread
 * 
 * Run it with sudo:
 *   sudo ./<this-file>.o -p|--poll-frequency <target_freq> -m|--map-path <path>
 * 
 * @author: xmei@jlab.org, ChatGPT
 * First checked in @date: July 16, 2025
 * Updated @date: Nov 11, 2025
 * @test on "nvidarm" with unidirectional traffic of up to 2000 Hz.
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

#include "json.hpp"      // external files downloaded online
#include "tc_common.h"   // header file for this project only


using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;


// ......... Default Command-Line Parameters ..............................
std::string map_path = "/sys/fs/bpf/tc-eg";
/// TODO: now it's fixed at reporting every 1 second.
/// Make it adjustable.
int export_interval = 1;    // in seconds
int poll_hz = 20;

const unsigned int SLOTS_IN_GLOBAL_RING_BUFFER = 60;
// ---------------------------------------


// ......... Data sttructure to snapshot and store bpf map values ........
// ++ Data structure for snapshot at fine-grained time ticks
struct LastSeen {
    __u64 tcp_bytes = 0;
    __u64 tcp_packets = 0;
    __u64 udp_bytes = 0;
    __u64 udp_packets = 0;
};

// ++ Data structure to store the snapshot values for every IP address
struct BinsPerIP {
    std::vector<__u64> tcp_bytes;
    std::vector<__u64> tcp_packets;
    std::vector<__u64> udp_bytes;
    std::vector<__u64> udp_packets;

    BinsPerIP() = default;
    explicit BinsPerIP(size_t n)
        : tcp_bytes(n, 0),
          tcp_packets(n, 0),
          udp_bytes(n, 0),
          udp_packets(n, 0) {};
};

// ++ Per coarse-grained data structure
std::map<uint32_t, BinsPerIP> window; 

std::array<decltype(window), SLOTS_IN_GLOBAL_RING_BUFFER> gBuffer;
// -----------------------------


// ......... Global Shared State ..................
std::atomic<bool> running(true);
void handle_signal(int) {
    running = false;
}

std::shared_mutex data_mutex;
bool first_report = true;


/**
 * @brief Return the timestamp in seconds since the UTC epoch (1970-01-01).
 */
inline time_t now_sec() {
    // Suggested by AI that it's the fastest way to return ts in seconds.
    return std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
}

/**
 * @brief A helper function to print the per-second raw time-series bins.
 */
void print_latest_metric_bin(const time_t print_second) {
    int window_id = print_second % SLOTS_IN_GLOBAL_RING_BUFFER;
    // std::unique_lock lock(data_mutex);

    auto metric_bins = gBuffer[window_id];
    if (metric_bins.empty()) {
        std::cout << "[metric_bins] is empty.\n";
        return;
    }

    std::cout << "\nLatest timestamp: " << print_second << std::endl;
    for (const auto& [ip, bins] : metric_bins) {
        std::cout << "\n  IP: " << ip << std::endl;

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
std::vector<__u64> get_diff_vector(
    const std::vector<__u64> snapshot, __u64& last_seen, size_t& valid_len) {
    std::vector<__u64> diff;
    valid_len = 0;
    if (snapshot.empty()) return diff;

    /* Helper print
    std::cout << "\t snapshot:";
    for (size_t i = 0; i < snapshot.size(); i++) {
        std::cout << snapshot[i] << ",";
    }
    std::cout << "\n\t last_seen=" << last_seen << std::endl;
    */

    // snapshot is an array looks like [199, 199, ..., 200, 0, ...]
    // It's non-decreasing until the zeros.

    std::unique_lock lock(data_mutex);

    // First element
    if (snapshot[0] < last_seen) {
        /// TODO: @test this senario can happen. When an IP was ejected from the
        // map before and appears again, pre may be larger than snapshot[i].
        /// TODO: @bug actually we do not know whether an IP has been ejected from
        // the kernel map or not. last_seen has no information on that.
        /// TODO: @bug Redesign, how to keep the kernel map and last_seen consistent.
        std::cout << "[WARNING]\tNew entry?! curr=" << snapshot[0] << ", pre="\
            << last_seen << ", i=0" << std::endl;
        last_seen = snapshot[0];
    }
    diff.push_back(snapshot[0] - last_seen);
    __u64 pre = snapshot[0];
    valid_len = 1;
    for (size_t i = 1; i < snapshot.size(); ++i) {
        // Skip tailing zeros (because of extra memory allocation)
        if (snapshot[i - 1] > 0 && snapshot[i] == 0)
            break;
        /// TODO: --verbose mode
        if (snapshot[i] < pre) {
            /// NOTE: We do have warnings when i is large!!!
            /// It happens when reusing the ring buffer slot. Each round the real
            //   bin numbers are different. If current round got less bins, then it will access
            //   the data from previous run of the same buffer slot.
            // std::cout << "[WARNING]\t [curr < pre?!] curr=" << snapshot[i] << ", pre="\
            // << pre << ", i=" << i << ", last_seen=" << last_seen << std::endl;
            last_seen = pre;
            break;
        }
        diff.push_back(snapshot[i] - pre);
        pre = snapshot[i];
        valid_len += 1;
    }
    last_seen = snapshot[valid_len - 1];

    /* Helper print
    std::cout << "\t diff:";
    for (size_t i = 0; i < diff.size(); i++) {
        std::cout << diff[i] << ",";
    }
    std::cout << "\n\t valid_len=" << valid_len << std::endl;
    std::cout << "\t last_seen=" << last_seen << std::endl;
    */

    bool all_zero = std::all_of(diff.begin(), diff.end(),
                            [](auto v){ return v == 0; });
    if (all_zero) {
        valid_len = 0;
        return {};
    }

    return diff;
}

/**
 * @brief Update a single traffic metric field in the per-IP JSON record and
 *        advance the corresponding last-seen counter.
 *
 * This helper function encapsulates the common logic used for both TCP/UDP
 * byte and packet metrics. It computes per-interval differences between the
 * current snapshot (`snapshot`) and the previously recorded cumulative value
 * (`last_seen_val`), stores the resulting difference vector into the JSON
 * object (`j_ip`), and updates `last_seen_val` to the latest cumulative value.
 *
 * The function only updates the JSON and `last_seen_val` if at least one
 * valid (non-zero) element is found in the snapshot.
 *
 * @param j_ip
 *        Reference to the per-IP JSON object being constructed.
 *        The function inserts a new key-value pair using `field_name`
 *        as the JSON field name.
 *
 * @param field_name
 *        Name of the metric field (e.g., `"tcp_bytes"`, `"tcp_packets"`,
 *        `"udp_bytes"`, `"udp_packets"`).
 *
 * @param snapshot
 *        Vector of cumulative counter values for the metric being updated.
 *        Typically taken from a `BinsPerIP` member (e.g., `bins.tcp_bytes`).
 *
 * @param last_seen_val
 *        Reference to the last cumulative value recorded for this metric
 *        and IP. The function updates this value in-place to the last
 *        non-zero snapshot entry processed.
 *
 * @note
 * - Calls `get_diff_vector()` internally to compute per-interval deltas.
 * - Does nothing if the snapshot vector is empty or contains only zeros.
 * - Designed for use within higher-level aggregation functions such as
 *   `print_in_json()`.
 */

inline void update_metric_field(
    json& j_ip,
    const std::string& field_name,
    const std::vector<__u64>& snapshot,
    __u64& last_seen_val)
{
    if (snapshot.empty())
        return;  // no data in this window

    size_t valid_len = 0;
    auto diff = get_diff_vector(snapshot, last_seen_val, valid_len);

    if (valid_len > 0) {
        j_ip[field_name] = diff;
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

    /// TODO: check if we can traverse faster by batching
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
 * @brief Append a polling snapshot of TCP and UDP traffic statistics into a specific
 *        time window within the global metric ring buffer.
 *
 * This function updates per-IP traffic metrics (bytes and packets) for a given
 * time window (`window_id`) and fine-grained polling tick (`polling_id`).
 * Each window in the global `gBuffer` corresponds to one slot of the ring buffer,
 * where each slot maps IPv4 addresses to their corresponding `BinsPerIP` data structure.
 *
 * The function ensures thread safety by acquiring a mutex lock internally before
 * modifying the global data structure.
 *
 * @param window_id
 *        Index of the current time window within the global ring buffer
 *        (0 ≤ window_id < SLOTS_IN_GLOBAL_RING_BUFFER).
 *
 * @param polling_id
 *        Index of the fine-grained time tick within the current window, indicating
 *        which position in the per-IP metric vectors will be updated.
 *        Must satisfy 0 ≤ polling_id < num_bins.
 *
 * @param num_bins
 *        Total number of bins per IP entry in a `BinsPerIP` instance. Used to
 *        initialize new `BinsPerIP` objects if the IP address is observed for
 *        the first time in the current window.
 *
 * @param snapshot_tcp
 *        Map of TCP traffic statistics, keyed by IPv4 address (in `uint32_t` form).
 *        Each value is a `traffic_val_t` structure containing `bytes` and `packets`
 *        fields, which are written to the corresponding TCP vectors in the
 *        `BinsPerIP` entry.
 *
 * @param snapshot_udp
 *        Map of UDP traffic statistics, keyed by IPv4 address (in `uint32_t` form).
 *        Each value is a `traffic_val_t` structure containing `bytes` and `packets`
 *        fields, which are written to the corresponding UDP vectors in the
 *        `BinsPerIP` entry.
 *
 * @note
 * - This function acquires `data_mutex` internally to serialize access to `gBuffer`.
 * - If an IP entry does not exist in the target window, a new `BinsPerIP`
 *   instance is created and initialized with `num_bins` elements per vector.
 * - The function assumes that `polling_id` is within range for all initialized
 *   vectors; no bounds checking is performed for performance reasons.
 * - The function overwrites existing values at the given `polling_id` index
 *   instead of accumulating them.
 */
void append_snapshot_to_metric_bins(
    const int window_id,
    const uint32_t polling_id,
    const int num_bins,
    // Map key: IP in integer
    const std::map<uint32_t, traffic_val_t>& snapshot_tcp,
    const std::map<uint32_t, traffic_val_t>& snapshot_udp) {

    std::unique_lock lock(data_mutex);  // Protect global access

    auto& curr_window = gBuffer[window_id];

    for (const auto& [ip, val] : snapshot_tcp) {
        auto& bins = curr_window[ip];

        // Initialize vectors if first time this IP appears
        if (bins.tcp_bytes.empty()) {
            /// NOTE: for large polling frequency, used bin number is smaller than num_bins
            bins = BinsPerIP(num_bins);
        }

        bins.tcp_bytes[polling_id] = val.bytes;
        bins.tcp_packets[polling_id] = val.packets;
    }

    for (const auto& [ip, val] : snapshot_udp) {
        auto& bins = curr_window[ip];

        // Initialize vectors if first time this IP appears
        if (bins.udp_bytes.empty()) {
            bins = BinsPerIP(num_bins);
        }

        bins.udp_bytes[polling_id] = val.bytes;
        bins.udp_packets[polling_id] = val.packets;
    }
}


/**
 * @brief Convert and print the per-IP traffic metrics of a specific window in JSON format.
 *
 * This function serializes the per-IP TCP/UDP byte and packet counters from the
 * ring buffer slot corresponding to `print_second` into a JSON record.
 * It compares each IP’s most recent counters against the last-seen values stored
 * in `last_seen` to compute per-interval deltas and outputs only updated entries.
 *
 * The resulting JSON object is structured as:
 * ```
 * {
 *   "<timestamp>": {
 *     "<ip>": {
 *       "tcp_bytes": [...],
 *       "tcp_packets": [...],
 *       "udp_bytes": [...],
 *       "udp_packets": [...]
 *     },
 *     ...
 *   }
 * }
 * ```
 *
 * @param print_second
 *        The timestamp (in seconds) identifying the window to be printed.
 *        This value is also used as the JSON record key.
 *
 * @param last_seen
 *        A reference to a map storing the last-seen per-IP counters from the
 *        previous print cycle. It is updated in-place with the latest counters
 *        after each call to track deltas between intervals.
 * @param verbose
 *        Helper print last_seen flag.
 *
 * @note
 * - The function accesses the global `gBuffer` to read per-IP bins.
 * - Only entries with nonzero changes since the previous print are included.
 * - Designed to be invoked asynchronously (e.g., via `std::thread(print_in_json, ...)`).
 * - The output is currently written to `stdout` in pretty-printed JSON form.
 */
void print_in_json(
    const time_t print_second, std::map<uint32_t, LastSeen>& last_seen, const bool verbose) {
    json j_ts;

    int window_id = print_second % SLOTS_IN_GLOBAL_RING_BUFFER;
    // Note that not all time windows have exactly poll_hz values
    // It may look like [9766, ..., 9766, 0, 0, 0]

    // First-time initialization to avoid first data-point spike.
    if (first_report) {
        for (const auto& [ip, bins] : gBuffer[window_id]) {
            last_seen[ip].tcp_bytes = bins.tcp_bytes.front();
            last_seen[ip].tcp_packets = bins.tcp_packets.front();
            last_seen[ip].udp_bytes = bins.udp_bytes.front();
            last_seen[ip].udp_packets = bins.udp_packets.front();
        }
        first_report = false;
    }

    // std::unique_lock lock(data_mutex);
    for (const auto& [ip, bins] : gBuffer[window_id]) {
        json j_ip;

        if (verbose) {
            std::cout << "<before> last_seen[" << ip << "] = (" << last_seen[ip].tcp_bytes <<\
            "[tcp_bytes], " << last_seen[ip].udp_bytes << "[udp_bytes])" << std::endl;
        }

        update_metric_field(j_ip, "tcp_bytes",   bins.tcp_bytes,   last_seen[ip].tcp_bytes);
        update_metric_field(j_ip, "tcp_packets", bins.tcp_packets, last_seen[ip].tcp_packets);
        update_metric_field(j_ip, "udp_bytes",   bins.udp_bytes,   last_seen[ip].udp_bytes);
        update_metric_field(j_ip, "udp_packets", bins.udp_packets, last_seen[ip].udp_packets);

        /// TODO: turn the debug information on for easier tracing
        /// TODO: Use last_seen to caculate the coarse-grain window sum
        if (!verbose) {
            std::cout << "[DEBUG] <after> last_seen[" << ip << "] = (" << last_seen[ip].tcp_bytes <<\
            "[tcp_bytes], " << last_seen[ip].udp_bytes << "[udp_bytes])" << std::endl;
        }

        if (j_ip.empty())
            continue;

        /// TODO: update this to include a (src, dst) pair
        j_ts[std::to_string(ip)] = j_ip;
    }

    // Reset this slot in the ring buffer to zeros
    for (auto& [ip, bins] : gBuffer[window_id]) {
        std::fill(bins.tcp_bytes.begin(),     bins.tcp_bytes.end(), 0);
        std::fill(bins.tcp_packets.begin(),   bins.tcp_packets.end(), 0);
        std::fill(bins.udp_bytes.begin(),     bins.udp_bytes.end(), 0);
        std::fill(bins.udp_packets.begin(),   bins.udp_packets.end(), 0);
    }
    if (j_ts.empty())
        return;

    json record;
    record[std::to_string(print_second)] = j_ts;

    /// TODO: not dump to screen
    std::cout << record.dump() << std::endl;
}


/*+....................................................................
CLI helper functions
*/
void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog <<" [-p poll-hz] [-m map-path]" << std::endl;
}

void parse_args(int argc, char** argv,
    int& poll_hz, std::string& map_path, bool& verbose) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--poll-hz") && i + 1 < argc) {
            poll_hz = std::stoi(argv[++i]);
        } else if ((arg == "-m" || arg == "--map-path") && i + 1 < argc) {
            map_path = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else {
            print_usage(argv[0]);
            exit(1);
        }
    }

    std::cout << "Poll the eBPF map at " << poll_hz << " Hz\n";
    std::cout << "Processing the eBPF map pinned at: " << map_path << "\n";
    std::cout << "Verbose mode: " << (verbose ? "ON" : "OFF") << "\n\n";
}
/* CLI helper functions
----------------------------------------------------------------*/


int main(int argc, char** argv) {
    bool verbose = false;
    parse_args(argc, argv, poll_hz, map_path, verbose);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::map<uint32_t, LastSeen> last_seen;

    // Sanity check for openning the eBPF map
    int map_fd = bpf_obj_get(map_path.c_str());
    if (map_fd < 0) {
        perror("Failed to open BPF map");
        exit(1);
    }

    /**
     * Continuously polls the eBPF map and aggregates the snapshots into time-series metric bins.
    */
    if (1000000 % poll_hz != 0) {
        std::cout << "Error, polling frequency not supported!";
        exit(-1);
    }
    auto interval_in_microseconds = std::chrono::microseconds(1000000 / poll_hz);

    time_t last_ts = now_sec();
    uint32_t polling_counter = 0;
    int window_id = -1;
    while (running) {
        std::map<uint32_t, traffic_val_t> snapshot_tcp;
        std::map<uint32_t, traffic_val_t> snapshot_udp;
        time_t curr_second = now_sec();

        // Using steady_clock() to measure time elapsed.
        /// TODO: ~1K cycles for timing @param elapsed
        ///       Check whether timing is necessary, or wrap it into a verbose mode.
        auto loop_start = std::chrono::steady_clock::now();
        if (curr_second != last_ts) {
            if (verbose) {
                std::cout << "### New tick: " << curr_second << ", window_id=" << window_id << std::endl;
                }
            // std::thread(print_latest_metric_bin, last_ts).detach();
            std::thread(print_in_json, last_ts, std::ref(last_seen), verbose).detach();
            polling_counter = 0;
            last_ts = curr_second;
        }

        window_id = curr_second % SLOTS_IN_GLOBAL_RING_BUFFER;
        if (get_snapshot_bpf_map(map_fd, snapshot_tcp, snapshot_udp) == 0) {
            append_snapshot_to_metric_bins(window_id, polling_counter, poll_hz,
                snapshot_tcp, snapshot_udp);
        }

        polling_counter += 1;

         // Sleep adjustment
        auto loop_end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start);

        if (verbose) {
            std::cout << "[INFO]\t[ " << curr_second << "]\t Polled_times = "\
                << polling_counter << ", elapsed_microseconds = " << elapsed.count() << std::endl;
        }

        if (elapsed < interval_in_microseconds) {
            std::this_thread::sleep_for(interval_in_microseconds - elapsed);
        }
    }

    return 0;
}
