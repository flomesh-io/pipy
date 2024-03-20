SRC_DIR = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BIN_DIR = $(abspath ${SRC_DIR})
INC_DIR = $(abspath ${SRC_DIR})

OUT = load-balancer.o
SRC = $(patsubst %.o,%.c,${OUT})

CFLAGS = \
	-O2 -D__KERNEL__ \
	-Wno-unused-value \
	-Wno-pointer-sign \
	-Wno-compare-distinct-pointer-types

all: ${BIN_DIR}/${OUT}

${BIN_DIR}/${OUT}: ${SRC_DIR}/${SRC}
	clang -I${INC_DIR} ${CFLAGS} -emit-llvm -c -g $< -o - | llc -march=bpf -filetype=obj -o $@

clean:
	rm -f ${BIN_DIR}/${OUT}
