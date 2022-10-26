ROOT_DIR = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PIPY_DIR = $(abspath ${ROOT_DIR}/../../..)

BIN_DIR = $(abspath ${PIPY_DIR}/bin)
INC_DIR = $(abspath ${PIPY_DIR}/include)

PRODUCT = ${BIN_DIR}/hello-nmi.so

OS = $(shell uname -s)

ifeq (${OS},Darwin)
  LDFLAGS = -Wl,-flat_namespace,-undefined,dynamic_lookup
endif

all: ${PRODUCT}

${PRODUCT}: ${ROOT_DIR}/hello.c
	clang -I${INC_DIR} ${LDFLAGS} -shared $< -o $@

clean:
	rm -f ${PRODUCT}

test:
	${BIN_DIR}/pipy main.js
