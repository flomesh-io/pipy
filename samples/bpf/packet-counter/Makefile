
DEBUGBPF = -DDEBUG
DEBUGFLAGS = -O0 -g -Wall
PFLAGS = $(DEBUGFLAGS)

TARGETS := pkt_counter

TARGETS_ALL = $(TARGETS)

# Generate file name-scheme based on TARGETS
KERN_SOURCES = ${TARGETS_ALL:=_kern.c}
KERN_OBJECTS = ${KERN_SOURCES:.c=.o}

LINUXINCLUDE += -I./headers

NOSTDINC_FLAGS =
#NOSTDINC_FLAGS := -nostdinc -isystem $(shell $(CC) -print-file-name='include')

EXTRA_CFLAGS =

LLC ?= llc
CLANG ?= clang
CC = gcc

kern: $(KERN_OBJECTS)

# Compiling of eBPF restricted-C code with LLVM
#  clang option -S generated output file with suffix .ll
#   which is the non-binary LLVM assembly language format
#   (normally LLVM bitcode format .bc is generated)
#
# Use -Wno-address-of-packed-member as eBPF verifier enforces
# unaligned access checks where necessary
#
$(KERN_OBJECTS): %.o: %.c Makefile
	$(CLANG) $(LINUXINCLUDE) $(EXTRA_CFLAGS) \
	-D__KERNEL__ -Wno-unused-value -Wno-pointer-sign \
	-Wno-compare-distinct-pointer-types \
	-O2 -emit-llvm -c -g $< -o -| $(LLC) -march=bpf -filetype=obj -o $@

clean:
	@find . -type f \
		\( -name '*~' \
		-o -name '*.ll' \
		-o -name '*.bc' \
		-o -name '*.o' \) \
		-exec rm -vf '{}' \;

format-c:
	find . -regex '.*\.\(c\|h\)' -exec clang-format -style=file -i {} \;

demo-attach:
	sudo ip link set lo xdpgeneric obj pkt_counter_kern.o sec xdp

demo-unattach:
	sudo ip link set lo xdpgeneric off

demo-map-dump:
	sudo bpftool map dump name pkt_cnt_stats -pj

demo-test:
	ping -c 5 127.0.0.8