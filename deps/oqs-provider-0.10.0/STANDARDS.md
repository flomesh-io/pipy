Standards supported when deploying oqsprovider
==============================================

For non-post-quantum algorithms, this provider is basically silent, i.e.,
permits use of standards and algorithms implemented by [openssl](https://github.com/openssl/openssl)
, e.g., concerning X.509, PKCS#8 or CMS.

For post-quantum algorithms, the version of the cryptographic algorithm used
depends on the version of [liboqs](https://github.com/open-quantum-safe/liboqs) used.
Regarding the integration of post-quantum algorithms into higher level
components, this provider implements the following standards:

- For TLS:
  - Hybrid post-quantum / traditional key exchange:
    - The data structures used follow the Internet-Draft [Hybrid key exchange in TLS 1.3](https://datatracker.ietf.org/doc/draft-ietf-tls-hybrid-design/), namely simple concatenation of traditional and post-quantum public keys and shared secrets.
    - The algorithm identifiers used are documented in [oqs-kem-info.md](https://github.com/open-quantum-safe/oqs-provider/blob/main/oqs-template/oqs-kem-info.md).
  - Hybrid post-quantum / traditional signatures in TLS:
    - For public keys and digital signatures inside X.509 certificates, see the bullet point on X.509 below.
    - For digital signatures outside X.509 certificates and in the TLS 1.3 handshake directly, the data structures used follow the same encoding format as that used for X.509 certificates, namely simple concatenation of traditional and post-quantum signatures.
    - The algorithm identifiers used are documented in [oqs-sig-info.md](https://github.com/open-quantum-safe/oqs-provider/blob/main/oqs-template/oqs-sig-info.md).
- For X.509:
  - Hybrid post-quantum / traditional public keys and signatures:
    - The data structures used follow the Internet-Draft [Internet X.509 Public Key Infrastructure: Algorithm Identifiers for Dilithium](https://datatracker.ietf.org/doc/draft-ietf-lamps-dilithium-certificates/), namely simple concatenation of traditional and post-quantum components in plain binary / OCTET_STRING representations.
    - The algorithm identifiers (OIDs) used are documented in [oqs-sig-info.md](https://github.com/open-quantum-safe/oqs-provider/blob/main/oqs-template/oqs-sig-info.md).
- For PKCS#8:
  - Hybrid post-quantum / traditional private keys:
    - Simple concatenation of traditional and post-quantum components in plain binary / OCTET_STRING representations.

Note: Please heed the [documentation on the enablement of KEM encoders](CONFIGURE.md#oqs_kem_encoders) via PKCS#8 and X.509.
