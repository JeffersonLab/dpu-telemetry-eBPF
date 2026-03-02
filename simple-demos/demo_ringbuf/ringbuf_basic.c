
#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>   // for __u32/__u64
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <linux/bpf.h>
#include <bpf/libbpf.h>    // bpf_object__*, bpf_program__*, ring_buffer__*

struct evt {
    __u64 ts_ns;
    __u32 pid;
    __u32 tgid;
    __u32 uid;
    __u32 gid;
};


static volatile sig_atomic_t stop;

static void on_sigint(int signo) { (void)signo; stop = 1; }


/**
 * @brief Callback invoked by libbpf when a new event is available in the eBPF ring buffer.
 *
 * This function is executed in userspace during `ring_buffer__poll()`. When the kernel
 * program submits an event via `bpf_ringbuf_submit()`, libbpf retrieves the record
 * from the ring buffer and invokes this callback to process the event payload.
 *
 * The function validates the record size, casts the raw buffer to `struct evt`,
 * and prints the event fields (process IDs, user/group IDs, and timestamp).
 *
 * @param ctx   Optional user-defined context pointer supplied to `ring_buffer__new()`.
 *              It can be used to pass application state to the callback.
 * @param data  Pointer to the event payload written by the eBPF program.
 *              The memory is owned by libbpf and valid only during this callback.
 * @param size  Size of the event payload in bytes.
 *
 * @return 0 to continue processing events. Non-zero values may interrupt polling.
 */
static int handle_sample(void *ctx, void *data, size_t size) {
    (void)ctx;
    if (size < sizeof(struct evt)) {
        fprintf(stderr, "short sample: %zu\n", size);
        return 0;
    }
    const struct evt *e = (const struct evt *)data;
    printf("ringbuf evt: tgid=%u pid=%u uid=%u gid=%u ts=%llu\n",
           e->tgid, e->pid, e->uid, e->gid, (unsigned long long)e->ts_ns);
    return 0;
}

int main(int argc, char **argv) {
    // hard-coded to match the eBPF executable
    const char *obj_path = (argc > 1) ? argv[1] : "./ringbuf_basic.bpf.o";
    struct bpf_object *obj = NULL;
    struct bpf_program *prog = NULL;
    struct bpf_link *link = NULL;
    struct ring_buffer *rb = NULL;
    int map_fd = -1;
    int err;

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

    // Find ringbuf map FD by name "rb"
    map_fd = bpf_object__find_map_fd_by_name(obj, "rb");
    if (map_fd < 0) {
        fprintf(stderr, "find map fd failed: %d\n", map_fd);
        return 1;
    }

    rb = ring_buffer__new(map_fd, handle_sample, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "ring_buffer__new failed: %d\n", errno);
        return 1;
    }

    /// NOTE: match the function name in *.bpf.c
    prog = bpf_object__find_program_by_name(obj, "handle_kernel_func");
    if (!prog) {
        fprintf(stderr, "find program failed\n");
        return 1;
    }

    link = bpf_program__attach(prog);
    if (!link) {
        fprintf(stderr, "attach failed\n");
        return 1;
    }

    printf("Attached. Run `getpid()` by doing anything (e.g., `echo hi`) and watch events. Ctrl-C to stop.\n");

    while (!stop) {
        err = ring_buffer__poll(rb, 100);          // timeout = 100ms
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "ring_buffer__poll error: %d\n", err);
            break;
        }
    }

    ring_buffer__free(rb);
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return 0;
}
