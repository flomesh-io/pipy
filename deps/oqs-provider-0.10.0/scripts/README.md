# Build and test support scripts

This directory contains various scripts aiming to ease build and test of `oqsprovider`.

## Building

The key file is [fullbuild.sh](fullbuild.sh) with options documented [here](https://github.com/open-quantum-safe/oqs-provider/blob/main/CONFIGURE.md#convenience-build-script-options).

## Testing

### API testing

All features and enabled algorithms are API tested by `ctest` driven code contained in the [test directory](https://github.com/open-quantum-safe/oqs-provider/tree/main/test).

### Command line testing

All features and enabled algorithms are tested via `openssl` command line instructions via the [runtests.sh](runtests.sh) script with options documented [here](https://github.com/open-quantum-safe/oqs-provider/blob/main/CONFIGURE.md#convenience-build-script-options).

### Release testing

All features and all algorithms can be tested in a full matrix running all possible signature and KEM algorithms in client/server setup via the corresponding `openssl s_server/s_client` commands via the [release-test.sh](release-test.sh) script. To run this test successfully, installation of `python3` and `pytest` with `xdist` extension is required, e.g., via `sudo apt install python3 python3-pytest python3-pytest-xdist python3-psutil`. The test must be executed within the main project directory, e.g., as such `./scripts/release-test.sh`. For full operation, a local and up-to-date (release) installation of `openssl` and `liboqs` (e.g., built via `scripts/fulltest.sh`) is recommended.
