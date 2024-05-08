PIPY_DIR = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PIPY_EXE = $(PIPY_DIR)/bin/pipy
PIPY_INSTALL = /usr/local/bin/pipy

ifneq (,$(shell command -v cmake3))
  CMAKE = cmake3
else
  CMAKE = cmake
endif

.PHONY: all full no-gui install

all: full

full:
	npm install
	npm run build
	mkdir -p build
	cd build && $(CMAKE) .. \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_COMPILER=clang \
	  -DCMAKE_CXX_COMPILER=clang++ \
	  -DPIPY_GUI=ON \
	  -DPIPY_CODEBASES=ON \
	  -DPIPY_BPF=ON \
	  -DPIPY_SOIL_FREED_SPACE=OFF \
	  -DPIPY_ASSERT_SAME_THREAD=OFF \
	  -DPIPY_LTO=OFF \
	&& $(MAKE)

no-gui:
	mkdir -p build
	cd build && $(CMAKE) .. \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_COMPILER=clang \
	  -DCMAKE_CXX_COMPILER=clang++ \
	  -DPIPY_GUI=OFF \
	  -DPIPY_CODEBASES=OFF \
	  -DPIPY_BPF=ON \
	  -DPIPY_SOIL_FREED_SPACE=OFF \
	  -DPIPY_ASSERT_SAME_THREAD=OFF \
	  -DPIPY_LTO=OFF \
	&& $(MAKE)

install: $(PIPY_INSTALL)

$(PIPY_INSTALL): $(PIPY_EXE)
	rm -f $(PIPY_INSTALL)
	cp -f $(PIPY_EXE) $(PIPY_INSTALL)
	chmod a+x $(PIPY_INSTALL)
