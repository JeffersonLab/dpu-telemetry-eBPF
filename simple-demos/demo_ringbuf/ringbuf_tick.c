// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
#define _GNU_SOURCE

#include <linux/types.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

/* Must match kernel */
struct tick_evt {
    __u64 ts_ns;
    __u64 seq_number;
};

static volatile sig_atomic_t stop;
static void on_sigint(int signo) { (void)signo; stop = 1; }

/* perf_event_open wrapper */
static int perf_event_open(struct perf_event_attr *attr,
                           pid_t pid, int cpu, int group_fd,
                           unsigned long flags) {
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/* ---- runtime options ---- */
struct opts {
    const char *obj_path;
    int cpu;
    int perf_freq; // Hz: perf-event trigger rate
    int bin_freq;  // Hz: userspace binning rate (derive bins from timestamps)
};


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Functions for command line inputs.
//
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--perf-freq N] [--bin-freq N] [--cpu C] [--obj PATH]\n"
        "  --perf-freq : perf event frequency in Hz (e.g., 1000, 4000, 10000)\n"
        "  --bin-freq  : userspace binning frequency in Hz (e.g., 1000, 4000)\n"
        "  --cpu       : CPU to attach perf event on (default 0)\n"
        "  --obj       : BPF object path (default ./ringbuf_tick.bpf.o)\n",
        prog);
}


static int parse_args(int argc, char **argv, struct opts *o) {
    static const struct option long_opts[] = {
        {"perf-freq", required_argument, NULL, 'p'},
        {"bin-freq",  required_argument, NULL, 'b'},
        {"cpu",       required_argument, NULL, 'c'},
        {"obj",       required_argument, NULL, 'o'},
        {"help",      no_argument,       NULL, 'h'},
        {0, 0, 0, 0}
    };

    /* defaults */
    o->obj_path = "./ringbuf_tick.bpf.o";
    o->cpu = 0;
    /// NOTE: It's bounded by `perf_event_max_sample_rate`.
    ///       If the input perf-freq is larger than this max, the program will not run.
    /// $ cat /proc/sys/kernel/perf_event_max_sample_rate ==> 50000 on ejfat-6
    o->perf_freq = 4000;
    o->bin_freq  = 1000;

    for (;;) {
        int idx = 0;
        int ch = getopt_long(argc, argv, "p:b:c:o:h", long_opts, &idx);
        if (ch == -1)
            break;

        switch (ch) {
        case 'p':
            o->perf_freq = atoi(optarg);
            break;
        case 'b':
            o->bin_freq = atoi(optarg);
            break;
        case 'c':
            o->cpu = atoi(optarg);
            break;
        case 'o':
            o->obj_path = optarg;
            break;
        case 'h':
        default:
            return -1;
        }
    }

    if (o->perf_freq <= 0 || o->bin_freq <= 0) {
        fprintf(stderr, "perf-freq and bin-freq must be > 0\n");
        return -1;
    }
    return 0;
}
// --------------------------------------------------------

/* ---- stats ---- */
static __u64 events_this_sec = 0;
static __u64 poll_calls_this_sec = 0;
static __u64 bins_this_sec = 0;      // number of bin transitions observed
static __u64 max_jitter_ns = 0;

static __u64 last_evt_ts = 0;
static __u64 perf_target_ns = 0;

static __u64 bin_ns = 0;
static __u64 last_bin_id = 0;
static int have_last_bin = 0;

static int handle_sample(void *ctx, void *data, size_t size) {
    (void)ctx;

    if (size < sizeof(struct tick_evt)) {
        fprintf(stderr, "short sample: %zu\n", size);
        return 0;
    }

    const struct tick_evt *e = (const struct tick_evt *)data;

    events_this_sec++;

    /* jitter vs perf tick target */
    if (last_evt_ts) {
        __u64 delta = e->ts_ns - last_evt_ts;
        __u64 jitter = (delta > perf_target_ns) ? (delta - perf_target_ns)
                                                : (perf_target_ns - delta);
        if (jitter > max_jitter_ns)
            max_jitter_ns = jitter;
    }
    last_evt_ts = e->ts_ns;

    /* binning by timestamp into fixed-width bins of size bin_ns */
    if (bin_ns) {
        __u64 bin_id = e->ts_ns / bin_ns;
        if (!have_last_bin) {
            have_last_bin = 1;
            last_bin_id = bin_id;
        } else if (bin_id != last_bin_id) {
            /*
             * Count how many bins we advanced.
             * If user-space falls behind, bin_id could jump by > 1.
             */
            bins_this_sec += (bin_id - last_bin_id);
            last_bin_id = bin_id;
        }
    }

    return 0;
}


int main(int argc, char **argv) {
    struct opts o;
    if (parse_args(argc, argv, &o) != 0) {
        usage(argv[0]);
        return 1;
    }

    /* derived targets */
    perf_target_ns = 1000000000ULL / (__u64)o.perf_freq;
    bin_ns         = 1000000000ULL / (__u64)o.bin_freq;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    struct bpf_object *obj = NULL;
    struct bpf_program *prog = NULL;
    struct bpf_link *link = NULL;
    struct ring_buffer *rb = NULL;

    int rb_map_fd = -1;
    int drops_map_fd = -1;
    int perf_fd = -1;
    int err;

    obj = bpf_object__open_file(o.obj_path, NULL);
    if (!obj) {
        fprintf(stderr, "open bpf obj failed: %s\n", strerror(errno));
        return 1;
    }

    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "load bpf obj failed: %d (%s)\n", err, strerror(-err));
        return 1;
    }

    /*  Find maps.  */
    // Do not need to find map "seq" because it serves as a global var only in kernel.
    rb_map_fd = bpf_object__find_map_fd_by_name(obj, "rb");
    if (rb_map_fd < 0) {
        fprintf(stderr, "find rb map fd failed: %d\n", rb_map_fd);
        return 1;
    }

    drops_map_fd = bpf_object__find_map_fd_by_name(obj, "drops");
    if (drops_map_fd < 0) {
        fprintf(stderr, "find drops map fd failed: %d\n", drops_map_fd);
        return 1;
    }

    rb = ring_buffer__new(rb_map_fd, handle_sample, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "ring_buffer__new failed: %s\n", strerror(errno));
        return 1;
    }

    /* Program name must match kernel: SEC("perf_event") int on_tick(...) */
    prog = bpf_object__find_program_by_name(obj, "on_tick");
    if (!prog) {
        fprintf(stderr, "find program 'on_tick' failed\n");
        return 1;
    }

    /* Create perf_event @ perf_freq on selected CPU */
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.size = sizeof(attr);
    // Together, below two lines produce N samples per second.
    // Because the eBPF kernel is tied to this perf_event, the eBPF kernel is also
    //   triggered N times per second.
    attr.freq = 1;
    attr.sample_freq = (unsigned int)o.perf_freq;  // N

    perf_fd = perf_event_open(&attr, -1, o.cpu, -1, 0);
    if (perf_fd < 0) {
        fprintf(stderr, "perf_event_open failed: %s\n", strerror(errno));
        return 1;
    }

    link = bpf_program__attach_perf_event(prog, perf_fd);
    if (!link) {
        fprintf(stderr, "attach perf_event failed: %s\n", strerror(errno));
        return 1;
    }

    err = ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0);
    if (err) {
        fprintf(stderr, "PERF_EVENT_IOC_ENABLE failed: %s\n", strerror(errno));
        return 1;
    }

    printf("Attached on CPU%d: perf_freq=%d Hz (target=%llu ns), bin_freq=%d Hz (bin=%llu ns). Ctrl-C to stop.\n",
           o.cpu, o.perf_freq, perf_target_ns,
           o.bin_freq, bin_ns);

    time_t last_print = time(NULL);   // in seconds
    __u64 last_drops = 0;

    while (!stop) {
        /*
         * Poll in smaller chunks to measure poll capacity too.
         */
        poll_calls_this_sec++;
        err = ring_buffer__poll(rb, 0);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "ring_buffer__poll error: %d\n", err);
            break;
        }

        time_t now = time(NULL);
        if (now != last_print) {  // another second starts
            __u32 k = 0;
            __u64 drops = 0;
            if (bpf_map_lookup_elem(drops_map_fd, &k, &drops) != 0)
                drops = 0;

            __u64 drops_this_sec = drops - last_drops;
            last_drops = drops;

            printf("[%lld] events/s=%llu  bins/s=%llu  poll_calls/s=%llu  drops/s=%llu  max_jitter_ns=%llu\n",
                   (long long)last_print,
                   events_this_sec, bins_this_sec, poll_calls_this_sec, drops_this_sec, max_jitter_ns);

            events_this_sec = 0;
            bins_this_sec = 0;
            poll_calls_this_sec = 0;
            max_jitter_ns = 0;
            last_print = now;
        }
    }

    ring_buffer__free(rb);
    bpf_link__destroy(link);
    if (perf_fd >= 0) close(perf_fd);
    bpf_object__close(obj);
    return 0;
}
