Windows-specific build/install instructions
========================================

`oqsprovider` can also be build on/for Windows using Microsoft Visual
Studio for C (MSVC) or `cygwin`. For the latter, please check the
[notes for Unix](NOTES-UNIX.md).

## Dependencies

### OpenSSLv3

OpenSSL (>=3.0.0) is not yet generally available under Windows. It is
therefore sensible to build it from scratch.

For that, please follow the instructions [here](https://github.com/openssl/openssl/blob/master/NOTES-WINDOWS.md).
A complete scripted setup is available in the [CI tooling for oqs-provider](https://github.com/open-quantum-safe/oqs-provider/blob/main/.github/workflows/windows.yml).

### liboqs

Instructions for building `liboqs` from source is available 
[here](https://github.com/open-quantum-safe/liboqs#windows).

## Build tooling

`oqsprovider` is best built on Windows when `git` access, `cmake`, `ninja` and
a C compiler are present, e.g., as in MS Visual Studio 2022.

## Build

A standard `cmake` build sequence can be used (assuming prerequisites are installed)
to build in/install from directory `_build`:

    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="/wd5105" -GNinja -S . -B _build && cd _build && ninja && ninja install

The specific `CMAKE_C_FLAGS` silence some overly strict warning messages and
the specific reference to the build type ensures a shared library with
release symbols, suitable for use with a release-symbol `openssl` build
is created.

If `openssl` and/or `liboqs` have not been installed to system standard locations
use the `cmake` defines "-DOPENSSL_ROOT_DIR" and/or "-Dliboqs_DIR" to utilize
those, e.g., like this:

    cmake -DOPENSSL_ROOT_DIR=c:\opt\openssl3 -Dliboqs_DIR=c:\liboqs -S . -B _build && cmake --build _build && cmake --install _build

Further configuration options are documented [here](CONFIGURE.md#build-install-options).

## Test

Standard `ctest` can be used to validate correct operation in build directory `_build`, e.g.:

    ctest -V --test-dir _build

## Packaging

Packaging the resultant .DLL is not yet implemented. Suggestions which package manager
to use are welcome.
