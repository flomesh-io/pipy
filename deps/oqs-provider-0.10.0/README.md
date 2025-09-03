[![GitHub actions](https://github.com/open-quantum-safe/oqs-provider/actions/workflows/linux.yml/badge.svg)](https://github.com/open-quantum-safe/oqs-provider/actions/workflows/linux.yml)
[![GitHub actions](https://github.com/open-quantum-safe/oqs-provider/actions/workflows/windows.yml/badge.svg)](https://github.com/open-quantum-safe/oqs-provider/actions/workflows/windows.yml)
[![GitHub actions](https://github.com/open-quantum-safe/oqs-provider/actions/workflows/macos.yml/badge.svg)](https://github.com/open-quantum-safe/oqs-provider/actions/workflows/macos.yml)
[![oqs-provider](https://circleci.com/gh/open-quantum-safe/oqs-provider.svg?style=svg)](https://app.circleci.com/pipelines/github/open-quantum-safe/oqs-provider)

oqsprovider - Open Quantum Safe provider for OpenSSL (3.x)
==========================================================

Purpose
-------

This repository contains code to enable quantum-safe cryptography (QSC)
in a standard OpenSSL (3.x) distribution by way of implementing a single
shared library, the OQS
[provider](https://www.openssl.org/docs/manmaster/man7/provider.html).

Status
------

Currently this provider fully enables quantum-safe cryptography for KEM
key establishment in TLS1.3 including management of such keys via the
OpenSSL (3.0) provider interface and hybrid KEM schemes. Also, QSC
signatures including CMS and CMP functionality are available via the OpenSSL
EVP interface. Key persistence is provided via the encode/decode mechanism,
X.509 data structures, and PKCS#12 for bundling a private key with its
corresponding X.509 certificate. Starting with OpenSSL 3.2 support for
TLS1.3 signature functionality is available and final glitches for CMS
have been resolved.

The standards implemented are documented in the separate file [STANDARDS.md](STANDARDS.md).

Algorithms
----------

This implementation makes available the following quantum safe algorithms:

<!--- OQS_TEMPLATE_FRAGMENT_ALGS_START -->
### KEM algorithms

- **BIKE**: `bikel1`, `p256_bikel1`, `x25519_bikel1`, `bikel3`, `p384_bikel3`, `x448_bikel3`, `bikel5`, `p521_bikel5`
- **FrodoKEM**: `frodo640aes`, `p256_frodo640aes`, `x25519_frodo640aes`, `frodo640shake`, `p256_frodo640shake`, `x25519_frodo640shake`, `frodo976aes`, `p384_frodo976aes`, `x448_frodo976aes`, `frodo976shake`, `p384_frodo976shake`, `x448_frodo976shake`, `frodo1344aes`, `p521_frodo1344aes`, `frodo1344shake`, `p521_frodo1344shake`
- **ML-KEM**: `mlkem512`, `p256_mlkem512`, `x25519_mlkem512`, `mlkem768`, `p384_mlkem768`, `x448_mlkem768`, `X25519MLKEM768`, `SecP256r1MLKEM768`, `mlkem1024`, `p521_mlkem1024`, `SecP384r1MLKEM1024`

### Signature algorithms


- **ML-DSA**:`mldsa44`, `p256_mldsa44`, `rsa3072_mldsa44`, `mldsa65`, `p384_mldsa65`, `mldsa87`, `p521_mldsa87`
- **Falcon**:`falcon512`, `p256_falcon512`, `rsa3072_falcon512`, `falconpadded512`, `p256_falconpadded512`, `rsa3072_falconpadded512`, `falcon1024`, `p521_falcon1024`, `falconpadded1024`, `p521_falconpadded1024`

- **SPHINCS-SHA2**:`sphincssha2128fsimple`, `p256_sphincssha2128fsimple`, `rsa3072_sphincssha2128fsimple`, `sphincssha2128ssimple`, `p256_sphincssha2128ssimple`, `rsa3072_sphincssha2128ssimple`, `sphincssha2192fsimple`, `p384_sphincssha2192fsimple`, `sphincssha2192ssimple`\*, `p384_sphincssha2192ssimple`\*, `sphincssha2256fsimple`\*, `p521_sphincssha2256fsimple`\*, `sphincssha2256ssimple`\*, `p521_sphincssha2256ssimple`\*
- **SPHINCS-SHAKE**:`sphincsshake128fsimple`, `p256_sphincsshake128fsimple`, `rsa3072_sphincsshake128fsimple`, `sphincsshake128ssimple`\*, `p256_sphincsshake128ssimple`\*, `rsa3072_sphincsshake128ssimple`\*, `sphincsshake192fsimple`\*, `p384_sphincsshake192fsimple`\*, `sphincsshake192ssimple`\*, `p384_sphincsshake192ssimple`\*, `sphincsshake256fsimple`\*, `p521_sphincsshake256fsimple`\*, `sphincsshake256ssimple`\*, `p521_sphincsshake256ssimple`\*
- **MAYO**:`mayo1`, `p256_mayo1`, `mayo2`, `p256_mayo2`, `mayo3`, `p384_mayo3`, `mayo5`, `p521_mayo5`
- **CROSS**:`CROSSrsdp128balanced`, `CROSSrsdp128fast`\*, `CROSSrsdp128small`\*, `CROSSrsdp192balanced`\*, `CROSSrsdp192fast`\*, `CROSSrsdp192small`\*, `CROSSrsdp256small`\*\*, `CROSSrsdpg128balanced`\*, `CROSSrsdpg128fast`\*, `CROSSrsdpg128small`\*, `CROSSrsdpg192balanced`\*, `CROSSrsdpg192fast`\*, `CROSSrsdpg192small`\*, `CROSSrsdpg256balanced`\*, `CROSSrsdpg256fast`\*, `CROSSrsdpg256small`\*
- **UOV**:`OV_Is`\*\*, `p256_OV_Is`\*\*, `OV_Ip`\*\*, `p256_OV_Ip`\*\*, `OV_III`\*\*, `p384_OV_III`\*\*, `OV_V`\*\*, `p521_OV_V`\*\*, `OV_Is_pkc`\*\*, `p256_OV_Is_pkc`\*\*, `OV_Ip_pkc`, `p256_OV_Ip_pkc`, `OV_III_pkc`\*\*, `p384_OV_III_pkc`\*\*, `OV_V_pkc`\*\*, `p521_OV_V_pkc`\*\*, `OV_Is_pkc_skc`\*\*, `p256_OV_Is_pkc_skc`\*\*, `OV_Ip_pkc_skc`, `p256_OV_Ip_pkc_skc`, `OV_III_pkc_skc`\*\*, `p384_OV_III_pkc_skc`\*\*, `OV_V_pkc_skc`\*\*, `p521_OV_V_pkc_skc`\*\*
- **SNOVA**:`snova2454`, `p256_snova2454`, `snova2454shake`\*\*, `p256_snova2454shake`\*\*, `snova2454esk`, `p256_snova2454esk`, `snova2454shakeesk`\*\*, `p256_snova2454shakeesk`\*\*, `snova37172`, `p256_snova37172`, `snova2583`\*\*, `p256_snova2583`\*\*, `snova56252`\*\*, `p384_snova56252`\*\*, `snova49113`\*\*, `p384_snova49113`\*\*, `snova3784`\*\*, `p384_snova3784`\*\*, `snova2455`, `p384_snova2455`, `snova60104`\*\*, `p521_snova60104`\*\*, `snova2965`, `p521_snova2965`

<!--- OQS_TEMPLATE_FRAGMENT_ALGS_END -->

As the underlying [liboqs](https://github.com/open-quantum-safe/liboqs)
at build time may be configured to not enable all algorithms, and the
underlying `openssl` installation at runtime may deliver some algorithms
already it is advisable to check the possible subset of algorithms actually
enabled via the standard commands, i.e.,
`openssl list -signature-algorithms -provider oqsprovider` and
`openssl list -kem-algorithms -provider oqsprovider`.

In addition, the above algorithms that ARE NOT denoted with either "\*" or "\*\*" ARE enabled for TLS operations.
The above algorithms that ARE denoted with "\*" ARE NOT enabled for
TLS operations; this [can be changed by modifying the
"enabled" flags in the main algorithm configuration file](CONFIGURE.md#pre-build-configuration). The above algorithms that ARE denoted with "\*\*" ARE NOT enabled; this cannot be changed as these algorithms are incompatible with with [RFC 8446](https://datatracker.ietf.org/doc/html/rfc8446).

In order to support parallel use of classic and quantum-safe cryptography 
this provider also provides different hybrid algorithms, combining classic
and quantum-safe methods.
There are two types of combinations:
The Hybrids are listed above with a prefix denoting a classic algorithm, e.g., for elliptic curve: "p256_".

A full list of algorithms, their interoperability code points and OIDs as well
as a method to dynamically adapt them, e.g., for interoperability testing are
documented in [ALGORITHMS.md](ALGORITHMS.md).

## Using with OpenSSL 3.5.0 

OpenSSL version 3.5.0 adds native support for:

-**ML-KEM**: `MLKEM512`, `MLKEM768`, `MLKEM1024`, `X25519MLKEM768`, `SecP256r1MLKEM768`, `X448MLKEM1024`, `SecP384r1MLKEM1024`

-**ML-DSA**: `MLDSA44`, `MLDSA65`, `MLDSA87`

When loaded with OpenSSL (version >= 3.5.0), oqsprovider (version >= 0.9.0) automatically disables the following liboqs algorithms: 

-**ML-DSA**: `mldsa44`, `mldsa65`, `mldsa87`, `mldsa_*`

-**ML-KEM**: `mlkem512`, `mlkem768`, `mlkem1024`, `X25519MLKEM768`, `SecP256r1MLKEM768`, `X448MLKEM1024`, `SecP384r1MLKEM1024`

The new OpenSSL implementations can be be used in such a oqsprovider configuration (OpenSSL >= 3.5.0 and oqsprovider >= 0.9.0) by accessing them through the following algorithm IDs: 

-**ML-DSA**:
```
2.16.840.1.101.3.4.3.17, id-ml-dsa-44, ML-DSA-44, MLDSA44
2.16.840.1.101.3.4.3.18, id-ml-dsa-65, ML-DSA-65, MLDSA65
2.16.840.1.101.3.4.3.19, id-ml-dsa-87, ML-DSA-87, MLDSA87
```

-**ML_KEM**: 
```
2.16.840.1.101.3.4.4.1, id-alg-ml-kem-512, ML-KEM-512, MLKEM512
2.16.840.1.101.3.4.4.2, id-alg-ml-kem-768, ML-KEM-768, MLKEM768
2.16.840.1.101.3.4.4.3, id-alg-ml-kem-1024, ML-KEM-1024, MLKEM1024
X25519MLKEM768
X448MLKEM1024
SecP256r1MLKEM768
SecP384r1MLKEM1024
```

Building and testing -- Quick start
-----------------------------------

All component builds and testing described in detail below can be executed by
running the scripts `scripts/fullbuild.sh` and `scripts/runtests.sh`
respectively (tested on Linux Ubuntu and Mint as well as MacOS).

By default, these scripts always build and test against the current OpenSSL `master` branch.

These scripts can be [configured by setting various variables](CONFIGURE.md#convenience-build-script-options). Please note that these scripts do _not_ install `oqsprovider`. This can be facilitated by running `cmake --install _build` (and following the [activation instructions](USAGE.md#activation).

Building and testing
--------------------

The below describes the basic build-test-install cycle using the standard
`cmake` tooling. Platform-specific notes are available for [UNIX](NOTES-UNIX.md)
(incl. MacOS and `cygwin`) and [Windows](NOTES-Windows.md).

## Configuration options

All options to configure `oqs-provider` at build- or run-time are documented
in [CONFIGURE.md](CONFIGURE.md).

## Pre-requisites

To be able to build `oqsprovider`, OpenSSL 3.0 and liboqs need to be installed.
It's not important where they are installed, just that they are. If installed
in non-standard locations, these must be provided when running `cmake` via
the variables "OPENSSL_ROOT_DIR" and "liboqs_DIR". See [CONFIGURE.md](CONFIGURE.md)
for details.

## Basic steps

    cmake -S . -B _build && cmake --build _build && ctest --test-dir _build && cmake --install _build
    
Using
-----

Usage of `oqsprovider` is documented in the separate [USAGE.md](USAGE.md) file.

Note on OpenSSL versions
------------------------

`oqsprovider` is written to ensure building on all versions of OpenSSL
supporting the provider concept. However, OpenSSL still is in active
development regarding features supported via the provider interface.
Therefore some functionalities documented above are only supported
with specific OpenSSL versions:

## 3.0/3.1

In these versions, CMS functionality implemented in providers is not
supported: The resolution of https://github.com/openssl/openssl/issues/17717
has not been not getting back-ported to OpenSSL3.0.

Also not supported in this version are provider-based signature algorithms
used during TLS1.3 operations as documented in https://github.com/openssl/openssl/issues/10512.

Also not fully supported in 3.0.2 is performance testing as per the openssl
`speed` command as documented in #385.

## 3.2 and greater

These versions have full support for all TLS1.3 operations using PQ algorithms
when deploying `oqsprovider`, particularly with regard to the use of signature
algorithms. This also includes support for the "OSSL_SIGNATURE_PARAM_CONTEXT_STRING"
parameter that had not been supported before and for which limited support in
single PQ algorithms is available since `liboqs` version 0.12.

## 3.4 and greater

These versions are expected to support the `openssl pkeyutl -encap/-decap`
syntax for testing key encapsulation and decapsulation for test purposes. To
use this option, OQS provider should be built with
[KEM encoding/decoding support](CONFIGURE.md#oqs_kem_encoders).
Also new in this version is the possibility to retrieve all currently
active TLS signature algorithms via a new `openssl list` option:
`openssl list -tls-signature-algorithms`.

## 3.5 and greater

These versions include support for the standard PQC algorithms ML-KEM, ML-DSA
and SLH-DSA. Accordingly, `oqsprovider` can no longer succeed registering
(O)IDs for these algorithms as these already exist. In addition, `oqsprovider`
functionally (e.g., support for several key formats) and non-functionally
(e.g., code quality) is not at par with the implementations for these
algorithms. Therefore, these, as well as their hybrid variants
are disabled at runtime upon detection of these being available in `openssl`.
The same algorithms will continue to work even using the same `oqsprovider`
binary in OpenSSL installations with a version older than 3.5.

This limitation might be resolved by implementing https://github.com/open-quantum-safe/oqs-provider/discussions/625.
given sufficient interest. Contributions very welcome.

## All versions

A limitation present in older OpenSSL versions is the number of default groups
supported: [At most 44 default groups may be specified](https://github.com/openssl/openssl/issues/23624)
, e.g., passing to [SSL_CTX_set1_groups](https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set1_groups.html).
Therefore caution is advised activating all KEMs supported by `oqsprovider`
via [the pre-build configuration facility](CONFIGURE.md#pre-build-configuration):
This may lead to `openssl` crashing, depending on the OpenSSL version used:
The problem is gone in OpenSSL "master" branch and in the respective branches
since the releases 3.3.0, 3.2.2., 3.1.6 and 3.0.14.

For [general OpenSSL implementation limitations, e.g., regarding provider feature usage and support,
see here](https://www.openssl.org/docs/man3.0/man7/migration_guide.html).

A problem basically related to any TLS server installation is the observed
[limitation to 64 TLS signature algorithms](https://github.com/open-quantum-safe/oqs-provider/issues/399)
by some TLS server implementations. Therefore, again caution is advised
[activating more than 64 PQ signature algorithms via the pre-build configuration facility](CONFIGURE.md#pre-build-configuration).

Governance & Contributions
--------------------------

Project governance is documented in [GOVERNANCE.md](GOVERNANCE.md) and contribution
policy is documented in [CONTRIBUTING.md](CONTRIBUTING.md).

Discussions
-----------

The policy of this project is that all discussions pertaining to changes in the
functional and non-functional aspects of `oqsprovider` shall take place in
`github`. References to external discussion fora are discouraged to retain the
free and open flow of thoughts unencumbered by potentially differing or changing
access or data retention policies by `github`-external chat forums.

Team
----

Contributors to the `oqsprovider` include:

- Michael Baentsch (initial author and maintainer; responsible for all code except as listed per specific contributions below)
- Christian Paquin (original OpenSSL111 OQS integrator)
- Richard Levitte (OpenSSL provider wizard and initial `cmake` setup)
- Basil Hess (hybrid KEM integration & pqcrystals/mayo OID management)
- Julian Segeth (some memory management improvements)
- Alex Zaslavsky (improvements on OpenSSL integration)
- Will Childs-Klein (improvements on Kyber hybrid OIDs)
- Thomas Bailleux (many build, CI and usage improvements for different platforms)
- Felipe Ventura (experimental composite sig integration)
- Iyán Méndez Veiga (PKCS#12 testing)
- Alessandro Barenghi (CROSS OIDs)

History
-------

Documentation on current and past releases ("code history") is documented in
the separate file [RELEASE.md](RELEASE.md).

Acknowledgments
---------------

`oqsprovider` came into existence as a branch of [oqs-openssl](https://github.com/open-quantum-safe/openssl)
as part of the OQS project initially led by Douglas Stebila and Michele
Mosca at the University of Waterloo but split off to become a separate
project catering to the [OpenSSL provider](https://www.openssl.org/docs/manmaster/man7/provider.html)
concept. With OQS joining [PQCA](https://pqca.org) `oqsprovider` also
was folded into that organization.

The `oqsprovider` project had been supported through the [NGI Assure Fund](https://nlnet.nl/assure),
a fund established by [NLnet](https://nlnet.nl) with financial
support from the European Commission's [Next Generation Internet programme](https://www.ngi.eu),
under the aegis of DG Communications Networks, Content and Technology
under grant agreement No 957073.

Financial support for the development of Open Quantum Safe had been provided
by Amazon Web Services and the Tutte Institute for Mathematics and Computing.

The OQS project would like to make a special acknowledgement to the companies who
had dedicated programmer time to contribute source code to OQS, including
Amazon Web Services, evolutionQ, Microsoft Research, Cisco Systems, and IBM Research.

Research projects which developed specific components of OQS have been
supported by various research grants, including funding from the Natural
Sciences and Engineering Research Council of Canada (NSERC); see
[here](https://openquantumsafe.org/papers/SAC-SteMos16.pdf) and
[here](https://openquantumsafe.org/papers/NISTPQC-CroPaqSte19.pdf)
for funding acknowledgments.

# Disclaimers

## Standard software disclaimer

THIS SOFTWARE IS PROVIDED WITH NO WARRANTIES, EXPRESS OR IMPLIED, AND
ALL IMPLIED WARRANTIES ARE DISCLAIMED, INCLUDING ANY WARRANTY OF
MERCHANTABILITY AND WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE.

## Standards compliance

This project follows the [NIST PQC standardization process](https://csrc.nist.gov/projects/post-quantum-cryptography) and aims to support experimentation with the various PQC algorithms under evaluation and in different stages of standardization by NIST. `oqsprovider` at this time cannot claim or prove adherence to any standards documents published. For more details, review the file [STANDARDS.md](STANDARDS.md) carefully. Most notably, hybrid implementations exclusively implemented in `oqsprovider` are at a pre-standard/draft stage only. Over time the project aims to provide standards compliance and solicits input by way of contributions to achieve this state.

## Component disclaimer

`oqsprovider` for the implementation of all pure PQC functionality
is completely dependent on [liboqs](https://github.com/open-quantum-safe/liboqs) and accordingly
cannot recommend any use beyond experimentation purposes:

WE DO NOT CURRENTLY RECOMMEND RELYING ON THIS SOFTWARE IN A PRODUCTION ENVIRONMENT OR TO PROTECT ANY SENSITIVE DATA. This software is meant to help with research and prototyping. While we make a best-effort approach to avoid security bugs, this library has not received the level of auditing and analysis that would be necessary to rely on it for high security use.

Further details and background available at:

[liboqs disclaimer](https://github.com/open-quantum-safe/liboqs#limitations-and-security)
