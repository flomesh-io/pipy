Usage instructions for oqsprovider
==================================

This file documents information required to properly utilize `oqsprovider`
after installation on a machine running `openssl` v3.

Beware that `oqsprovider` will not work on machines where an OpenSSL
version below "3.0.0" is (the default) installed.

## Baseline assumption

An `openssl` version >= 3.0.0 is available and set in the "PATH" environment
variable such as that the command `openssl version` yields a result documenting
this, e.g., as follows:

```
OpenSSL 3.2.0-dev  (Library: OpenSSL 3.2.0-dev )
```

### Note on OpenSSL installation

If one does not have an OpenSSL version installed in a suitable version, care
is advised installing such version such as not to damage a pre-installed/system-wide
`openssl` installation.

In order to experiment with a local `openssl` version, we have made available
[a shell script](scripts/fullbuild.sh) creating a local, non-system wide installed
`openssl` binary. By default, the current "master" branch is built by this script
but it can be configured to build any release/tag by setting the [OPENSSL_BRANCH](CONFIGURE.md#openssl_branch)
environment variable.

## Activation

Every OpenSSL provider needs to be activated for use. There are three main ways
for this:

### Explicit command line option

#### -provider

Most `openssl` commands permit passing the option `-provider`: The name after
this command is that of the provider to be activated.

As an example: `openssl list -signature-algorithms -provider oqsprovider`
outputs all quantum safe signature algorithms made available for `openssl` use.

#### -provider-path

All `openssl` commands accepting `-provider` also permit passing `-provider-path`
as a possibility to reference the location in the local filesystem where the
provider binary is located. This is of particular use if the provider did not
(yet) get installed in the system location, which typically is in `lib/ossl-modules`
in the main `openssl` installation tree.

### C API

If activation of `oqsprovider` should be hard-coded in a C program, use of the
standard [OSSL_PROVIDER_load](https://www.openssl.org/docs/man3.1/man3/OSSL_PROVIDER_load.html)
API may be considered, e.g., as such:

    OSSL_PROVIDER_load(OSSL_LIB_CTX_new(), "oqsprovider");

The provider (binary) search path may be set via the [OSSL_PROVIDER_set_default_search_path](https://www.openssl.org/docs/manmaster/man3/OSSL_PROVIDER_set_default_search_path.html) API.

#### Static linking

If all code should be "baked" into one executable, be sure to statically build
`oqsprovider` using the `OQS_PROVIDER_BUILD_STATIC` configuration option and use
the integration code [documented](CONFIGURE.md#oqs_provider_build_static).

### Configuration file

As an alternative to passing command line parameters or hard-coding C code, providers
can be activated for general use by adding instructions to the `openssl.cnf` file.
In the case of `oqs-provider` add these lines to achieve this:

```
[provider_sect]
default = default_sect
oqsprovider = oqsprovider_sect
[oqsprovider_sect]
activate = 1
```

#### module option

Next to the "activate" keyword, `openssl` also recognizes the "module" keyword
which mirrors the functionality of `-provider-path` documented above: This way,
a non-standard location for the `oqsprovider` shared library (.SO/.DYLIB/.DLL)
can be registered for testing.

If this configuration variable is not set, the global environment variable
"OPENSSL_MODULES" must point to a directory where the `oqsprovider` binary
is to be found.

If the `oqsprovider` binary cannot be found, it simply (and silently) will
not be available for use.

#### System wide installation

The system-wide `openssl.cnf` file is typically located at (operating system dependent):
- /etc/ssl/ (UNIX/Linux)
- /opt/homebrew/etc/openssl@3/ (MacOS Homebrew on Apple Silicon)
- /usr/local/etc/openssl@3/ (MacOS Homebrew on Intel Silicon)
- C:\Program Files\Common Files\SSL\ (Windows)

Adding `oqsprovider` to this file will enable its seamless operation alongside other
`openssl` providers. If successfully done, running, e.g., `openssl list -providers`
should output something along these lines (version IDs variable of course):

```
providers:
  default
    name: OpenSSL Default Provider
    version: 3.1.1
    status: active
  oqsprovider
    name: OpenSSL OQS Provider
    version: 0.5.0
    status: active
```

If this is the case, all `openssl` commands can be used as usual, extended
by the option to use quantum safe cryptographic algorithms in addition/instead
of classical crypto algorithms.

This configuration is the one used in all examples below.

*Note*: Be sure to always activate either the "default" or "fips" provider as these
deliver functionality also needed by `oqsprovider` (e.g., for hashing or high
quality random data during key generation).

## Selecting TLS1.3 default groups

For activating specific [KEMs](README.md#kem-algorithms), three options exist:

### Command line parameter

All commands allowing pre-selecting KEMs for use permit this via the standard
OpenSSL [-groups](https://www.openssl.org/docs/manmaster/man3/SSL_CONF_cmd.html) command line option.
See example commands [below](#running-a-client-to-interact-with-quantum-safe-kem-algorithms).

### C API

If activation of specific KEM groups should be hard-coded into a C program,
use the standard OpenSSL [SSL_set1_groups_list](https://www.openssl.org/docs/manmaster/man3/SSL_set1_groups_list.html)
API, e.g., as such:

    SSL_set1_groups_list(ssl, "kyber768:kyber1024");

### Configuration parameter

The set of acceptable KEM groups can also be set in the `openssl.cnf` file
as per this example:

```
[openssl_init]
ssl_conf = ssl_sect

[ssl_sect]
system_default = system_default_sect

[system_default_sect]
Groups = kyber768:kyber1024
```

Be sure to separate permissible KEM names by colon if specifying several.

## Sample commands

The following section provides example commands for certain standard OpenSSL operations.

### Checking provider version information

    openssl list -providers -verbose

### Checking quantum safe signature algorithms available for use

    openssl list -signature-algorithms -provider oqsprovider

### Checking quantum safe KEM algorithms available for use

    openssl list -kem-algorithms -provider oqsprovider

### Creating keys and certificates

This can be facilitated for example by using the usual `openssl` commands:

    openssl req -x509 -new -newkey dilithium3 -keyout dilithium3_CA.key -out dilithium3_CA.crt -nodes -subj "/CN=test CA" -days 365 -config openssl/apps/openssl.cnf
    openssl genpkey -algorithm dilithium3 -out dilithium3_srv.key
    openssl req -new -newkey dilithium3 -keyout dilithium3_srv.key -out dilithium3_srv.csr -nodes -subj "/CN=test server" -config openssl/apps/openssl.cnf
    openssl x509 -req -in dilithium3_srv.csr -out dilithium3_srv.crt -CA dilithium3_CA.crt -CAkey dilithium3_CA.key -CAcreateserial -days 365

These examples create QSC dilithium3 keys but the very same commands can be used
to create QSC certificates replacing the key type "dilithium3" with any of the QSC
[signature algorithms supported](README.md#signature-algorithms).
Of course, also any classic signature algorithm like "rsa" may be used.

Note: While generating QSC certificates is supported with OpenSSL 3.0+, using 
QSC CA's and server certificates is not supported in versions prior to OpenSSL 3.2.
Refer to [Note on OpenSSL versions](README.md#note-on-openssl-versions).

### Setting up a (quantum-safe) test server

Using keys and certificates as created above, a simple server utilizing a
quantum-safe (QSC) KEM algorithm and certicate can be set up for example by running

    openssl s_server -cert dilithium3_srv.crt -key dilithium3_srv.key -www -tls1_3 -groups kyber768:frodo640shake

Instead of "dilithium3" any [QSC signature algorithm supported](README.md#signature-algorithms)
may be used as well as any classic crypto signature algorithm.
Instead of "kyber768:frodo640shake" any combination of [QSC KEM algorithm(s)](README.md#kem-algorithms)
and classic crypto KEM algorithm(s) may be specified.

### Running a client to interact with (quantum-safe) KEM algorithms

This can be facilitated for example by running

    openssl s_client -groups frodo640shake

By issuing the command `GET /` the quantum-safe crypto enabled OpenSSL3
server returns details about the established connection.

Any [available quantum-safe KEM algorithm](README.md#kem-algorithms) can be selected by passing it in the `-groups` option.

### S/MIME message signing -- Cryptographic Message Syntax (CMS)

Also possible is the creation and verification of quantum-safe digital
signatures using [CMS](https://datatracker.ietf.org/doc/html/rfc5652).

#### Signing data

For creating signed data, two steps are required: One is the creation
of a certificate using a QSC algorithm; the second is the use of this
certificate (and its signature algorithm) to create the signed data:

Step 1: Create quantum-safe key pair and self-signed certificate:

    openssl req -x509 -new -newkey dilithium3 -keyout qsc.key -out qsc.crt -nodes -subj "/CN=oqstest" -days 365 -config openssl/apps/openssl.cnf

By changing the `-newkey` parameter algorithm name [any of the
supported quantum-safe or hybrid algorithms](README.md#signature-algorithms)
can be utilized instead of the sample algorithm `dilithium3`.

Step 2: Sign data:

As
[the CMS standard](https://datatracker.ietf.org/doc/html/rfc5652#section-5.3)
requires the presence of a digest algorithm, while quantum-safe crypto
does not, in difference to the QSC certificate creation command above,
passing a message digest algorithm via the `-md` parameter is mandatory.

    openssl cms -in inputfile -sign -signer qsc.crt -inkey qsc.key -nodetach -outform pem -binary -out signedfile -md sha512

Data to be signed is to be contained in the file named `inputfile`. The
resultant CMS output is contained in file `signedfile`. The QSC algorithm
used is the same signature algorithm utilized for signing the certificate
`qsc.crt`.

#### Verifying data

Continuing the example above, the following command verifies the CMS file
`signedfile` and outputs the `outputfile`. Its contents should be identical
to the original data in `inputfile` above.

    openssl cms -verify -CAfile qsc.crt -inform pem -in signedfile -crlfeol -out outputfile

Note that it is also possible to build proper QSC certificate chains
using the standard OpenSSL calls. For sample code see
[scripts/oqsprovider-certgen.sh](scripts/oqsprovider-certgen.sh).

### Support of `dgst` (and sign)

Also tested to operate OK is the [openssl dgst](https://www.openssl.org/docs/man3.0/man1/openssl-dgst.html)
command. Sample invocations building on the keys and certificate files in the examples above:

#### Signing

    openssl dgst -sign qsc.key -out dgstsignfile inputfile

#### Verifying

    openssl dgst -signature dgstsignfile -verify qsc.pubkey inputfile

The public key can be extracted from the certificate using standard openssl command:

    openssl x509 -in qsc.crt -pubkey -noout > qsc.pubkey

The `dgst` command is not tested for interoperability with [oqs-openssl111](https://github.com/open-quantum-safe/openssl).

*Note on KEM Decapsulation API*:

The OpenSSL [`EVP_PKEY_decapsulate` API](https://www.openssl.org/docs/manmaster/man3/EVP_PKEY_decapsulate.html) specifies an explicit return value for failure. For security reasons, most KEM algorithms available from liboqs do not return an error code if decapsulation failed. Successful decapsulation can instead be implicitly verified by comparing the original and the decapsulated message.

## Supported OpenSSL parameters (`OSSL_PARAM`)

OpenSSL 3 comes with the [`OSSL_PARAM`](https://www.openssl.org/docs/man3.2/man3/OSSL_PARAM.html) API.
Through these [`OSSL_PARAM`] structures, oqs-provider can expose some useful information
about a specific object.

### `EVP_PKEY`

Using the [`EVP_PKEY_get_params`](https://www.openssl.org/docs/man3.2/man3/EVP_PKEY_get_params.html)
API, the following custom parameters are gettable:

  - `OQS_HYBRID_PKEY_PARAM_CLASSICAL_PUB_KEY`: points to the public key of the
    classical part of an hybrid key.
  - `OQS_HYBRID_PKEY_PARAM_CLASSICAL_PRIV_KEY`: points to the private key of the
    classical part of an hybrid key.
  - `OQS_HYBRID_PKEY_PARAM_PQ_PUB_KEY`: points to the public key of the
    quantum-resistant part of an hybrid key.
  - `OQS_HYBRID_PKEY_PARAM_PQ_PRIV_KEY`: points to the private key of the
    quantum-resistant part of an hybrid key.

In case of non hybrid keys, these parameters return `NULL`.

See the [corresponding test](tests/oqs_test_evp_pkey_params.c) for an example of
how to use [`EVP_PKEY_get_params`] with custom oqs-provider parameters.
