# Development guidelines

## Basics

Every developer has their own coding style and diversity in general is good and welcome.

We nevertheless do try to follow some basic goals in this project:

- All pieces should be readable / understandable without having to comprehend all parts first.
- Therefore, comments (incl. cross references where sensible) are encouraged.
- For syntactical legibility the project adopted the [OpenSSL coding convention](https://www.openssl.org/policies/technical/coding-style.html)
- Tooling exists to validate the coding convention: Simply execute `clang-format --dry-run --Werror file-to-test`
- Platform-specific code should be avoided to the greatest extent possible as the project aims to run correctly at least on Linux, MacOS and Windows (x64 and aarch64 architectures).

## Generated code

Significant parts of the code are generated via the script `oqs-template/generate.py`.
This script serves to import a specific version of [liboqs](https://github.com/open-quantum-safe/liboqs)
into `oqsprovider`. Most notably the control file `oqs-template/generate.yml` has to be
in sync with the specific `liboqs` version: algorithm IDs, e.g., signature algorithm
OIDs need to be aligned with the specific algorithm code version.
Therefore, no code within the generator brackets must be changed:

```
///// OQS_TEMPLATE_FRAGMENT_..._START
...
///// OQS_TEMPLATE_FRAGMENT_..._END
```

If such code changes are required they have to be implemented in the generator code
fragments located in the `oqs-template` directory.

During normal code development it is very unlikely any of these files need to be touched.

## Plain build

If the prerequisites for `oqsprovider` are met on a development machine, i.e.
presence of `liboqs` and `openssl` (v.3) the build can simply be executed by
running `scripts/fullbuild.sh`. Various parameters exist and are documented
in the script to adapt to a specific build environment and in [the documentation](CONFIGURE.md#convenience-build-script-options).
The script can also be used to build a specific `openssl` and a specific `liboqs`
version as well as debug versions of all components.

## Plain test

All tests meant for local feature testing are integrated/made available for
execution in the script `scripts/runtest.sh`. PRs should only be considered
if all tests pass locally as the CI system uses them too.

## Debugging

Project-specific debugging facilities are documented in [the wiki](https://github.com/open-quantum-safe/oqs-provider/wiki/Debugging).

For "classic" `gdb` style debugging, be certain to set "-DCMAKE_BUILD_TYPE=Debug"
when building `oqsprovider` and `-d` when configuring `openssl` (see
"scripts/fullbuild.sh" for further information where best to do this).


