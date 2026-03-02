#
# Compile the kernel and userspace code
#   - Kernel: ringbuf_xxx.bpf.c --> ringbuf_xxx.bpf.o
#   - Userspace: ringbuf_xxx.c --> ringbuf_xxx

CLANG ?= clang
CC    ?= gcc

# Make sure the filenames are correct before use!
####### Group 1: ringbuf + perfevent benchmarking
BPF_SRCFILE = ringbuf_tick.bpf.c
USER_SRCFILE = ringbuf_tick.c
BPF_OBJ = ringbuf_tick.bpf.o
USER_BIN = ringbuf_tick.o

####### Group 2: a basic ringbuf test triggered by syscalls
# BPF_SRCFILE = ringbuf_basic.bpf.c
# USER_SRCFILE = ringbuf_basic.c
# BPF_OBJ = ringbuf_basic.bpf.o
# USER_BIN = ringbuf_basic.o

CFLAGS += -O2 -g -Wall -Wextra
BPF_CFLAGS += -O2 -g -target bpf -D__TARGET_ARCH_x86

# all: $(BPF_OBJ) $(USER_BIN)
all: $(BPF_OBJ)

# Corresponding command: `sudo clang -O2 -g -target bpf $INC -c "$cfile" -o "$obj"`
$(BPF_OBJ): $(BPF_SRCFILE)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(USER_BIN): $(USER_SRCFILE)
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs libbpf)

clean:
	rm -f $(BPF_OBJ) $(USER_BIN)

run: all
	sudo ./$(USER_BIN) ./$(BPF_OBJ)
