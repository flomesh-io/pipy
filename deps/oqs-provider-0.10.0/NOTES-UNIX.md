UNIX-specific build/install instructions
========================================

`oqsprovider` is first and foremost being developed and maintained under
Linux. Therefore, all UNIX-like builds, incl. `cygwin`, should work with
least problems.

## Dependencies

### OpenSSLv3

OpenSSL (>=3.0.0) is becoming generally available via the various package
managers and distributions, e.g., via `apt install openssl` or `brew install openssl@3`.

If it is not, please build and install via the instructions [here](https://github.com/openssl/openssl/blob/master/NOTES-UNIX.md).

### liboqs

`liboqs` is available in some select distributions and package managers,
e.g., via `brew install liboqs` on MacOS, but typically needs to be build
from source. See instructions [here](https://github.com/open-quantum-safe/liboqs#linuxmacos).

## Build tooling

`oqsprovider` at minimum needs `git` access, `cmake` and a C compiler
to be present to be build, e.g., via `apt install cmake build-essential git`.

## Build

Standard `cmake` build sequence can be used (assuming prerequisites are installed)
to build in/install from directory `_build`:

    cmake -S . -B _build && cmake --build _build && cmake --install _build

If `openssl` and/or `liboqs` have not been installed to system standard locations
use the `cmake` define "-DOPENSSL_ROOT_DIR" and/or the environment variable 
"liboqs_DIR" to utilize those, e.g., like this:

    liboqs_DIR=../liboqs cmake -DOPENSSL_ROOT_DIR=/opt/openssl3 -S . -B _build && cmake --build _build && cmake --install _build

Further configuration options are documented [here](CONFIGURE.md#build-install-options).

## Test

Standard `ctest` can be used to validate correct operation in build directory `_build`, e.g.:

    cd _build && ctest --parallel 5 --rerun-failed --output-on-failure -V

## Packaging

### Debian

A build target to create UNIX .deb packaging is available via the standard
`package` target, e.g., executing `make package` in the `_build` subdirectory.
The resultant file can be installed as usual via `dpkg -i ...`.

### MacOS

An ".rb" packaging script for `brew` is available in the `scripts` directory
and is regularly tested as part of CI.
