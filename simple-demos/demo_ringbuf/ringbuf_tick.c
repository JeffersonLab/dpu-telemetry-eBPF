// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
#define _GNU_SOURCE
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <sys/syscall.h>
#include <linux/perf_event.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>


static volatile sig_atomic_t stop;
static void on_sigint(int signo) { (void)signo; stop = 1; }

/* ---- perf_event_open wrapper ---- */
static int perf_event_open(
    struct perf_event_attr *attr,
    pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/* ---- stats ---- */
static unsigned long long bins_this_sec = 0;
static unsigned long long max_jitter_ns = 0;
static unsigned long long last_ts = 0;
static unsigned long long target_ns = 0;

static int handle_sample(void *ctx, void *data, size_t size)
{
    (void)ctx;
    if (size < sizeof(struct evt)) {
        fprintf(stderr, "short sample: %zu\n", size);
        return 0;
    }
    const struct evt *e = (const struct evt *)data;

    bins_this_sec++;

    if (last_ts) {
        unsigned long long delta = (unsigned long long)(e->ts_ns - last_ts);
        unsigned long long jitter = (delta > target_ns) ? (delta - target_ns) : (target_ns - delta);
        if (jitter > max_jitter_ns)
            max_jitter_ns = jitter;
    }
    last_ts = (unsigned long long)e->ts_ns;

    // 不要每条 printf（会成为瓶颈）
    return 0;
}

int main(int argc, char **argv)
{
    const char *obj_path = "./ringbuf_tick.bpf.o";
    int freq = 1000; // default 1ms

    if (argc >= 2)
        freq = atoi(argv[1]); // 用法：./ringbuf_tick_user 1000|10000
    if (freq != 1000 && freq != 10000) {
        fprintf(stderr, "Usage: %s 1000|10000\n", argv[0]);
        return 1;
    }

    target_ns = (unsigned long long)(1000000000ULL / (unsigned long long)freq); // 1e9/freq

    struct bpf_object *obj = NULL;
    struct bpf_program *prog = NULL;
    struct bpf_link *link = NULL;
    struct ring_buffer *rb = NULL;

    int rb_map_fd = -1;
    int drops_map_fd = -1;
    int err;

    int perf_fd = -1;   // 我们只 attach CPU0（避免 *NCPU 放大）
    int cpu = 0;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    obj = bpf_object__open_file(obj_path, NULL);
    if (!obj) {
        fprintf(stderr, "open bpf obj failed\n");
        return 1;
    }

    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "load bpf obj failed: %d (%s)\n", err, strerror(-err));
        return 1;
    }

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
        fprintf(stderr, "ring_buffer__new failed: %d\n", errno);
        return 1;
    }

    // BPF program name must match: handle_kernel_func
    prog = bpf_object__find_program_by_name(obj, "handle_kernel_func");
    if (!prog) {
        fprintf(stderr, "find program failed\n");
        return 1;
    }

    // Create perf_event @ freq on CPU0
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK; // 或 PERF_COUNT_SW_TASK_CLOCK
    attr.size = sizeof(attr);

    attr.freq = 1;
    attr.sample_freq = (unsigned int)freq;

    // pid=-1 means "all processes" on that CPU
    perf_fd = perf_event_open(&attr, -1, cpu, -1, 0);
    if (perf_fd < 0) {
        fprintf(stderr, "perf_event_open failed: %s\n", strerror(errno));
        return 1;
    }

    // attach perf_event program to perf_fd
    link = bpf_program__attach_perf_event(prog, perf_fd);
    if (!link) {
        fprintf(stderr, "attach perf_event failed\n");
        return 1;
    }

    // enable the perf event
    err = ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0);
    if (err) {
        fprintf(stderr, "PERF_EVENT_IOC_ENABLE failed: %s\n", strerror(errno));
        return 1;
    }

    printf("Attached perf_event tick on CPU%d at %d Hz (target %llu ns). Ctrl-C to stop.\n",
           cpu, freq, target_ns);

    time_t last_print = time(NULL);
    unsigned long long last_drops = 0;

    while (!stop) {
        // 阻塞式/低开销等待：1000ms 超时便于每秒打印统计；不是 busy poll
        err = ring_buffer__poll(rb, 1000);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "ring_buffer__poll error: %d\n", err);
            break;
        }

        time_t now = time(NULL);
        if (now != last_print) {
            __u32 k = 0;
            unsigned long long drops = 0;
            if (bpf_map_lookup_elem(drops_map_fd, &k, &drops) != 0)
                drops = 0;

            unsigned long long drops_this_sec = drops - last_drops;
            last_drops = drops;

            printf("bins/s=%llu  drops/s=%llu  max_jitter_ns=%llu\n",
                   bins_this_sec, drops_this_sec, max_jitter_ns);

            bins_this_sec = 0;
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
