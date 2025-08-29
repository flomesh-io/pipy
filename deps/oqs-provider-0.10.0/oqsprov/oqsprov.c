// SPDX-License-Identifier: Apache-2.0 AND MIT

/*
 * OQS OpenSSL 3 provider
 *
 * Code strongly inspired by OpenSSL legacy provider.
 *
 */

#include <openssl/core.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/objects.h>
#include <openssl/params.h>
#include <openssl/provider.h>
#include <stdio.h>
#include <string.h>

#include "oqs_prov.h"

#ifdef NDEBUG
#define OQS_PROV_PRINTF(a)
#define OQS_PROV_PRINTF2(a, b)
#define OQS_PROV_PRINTF3(a, b, c)
#else
#define OQS_PROV_PRINTF(a)                                                     \
    if (getenv("OQSPROV"))                                                     \
    printf(a)
#define OQS_PROV_PRINTF2(a, b)                                                 \
    if (getenv("OQSPROV"))                                                     \
    printf(a, b)
#define OQS_PROV_PRINTF3(a, b, c)                                              \
    if (getenv("OQSPROV"))                                                     \
    printf(a, b, c)
#endif // NDEBUG

static STACK_OF(OPENSSL_STRING) *rt_disabled_algs = NULL;
STACK_OF(OPENSSL_STRING) * oqsprov_get_rt_disabled_algs() {
    return rt_disabled_algs;
}

/*
 * Forward declarations to ensure that interface functions are correctly
 * defined.
 */
static OSSL_FUNC_provider_gettable_params_fn oqsprovider_gettable_params;
static OSSL_FUNC_provider_get_params_fn oqsprovider_get_params;
static OSSL_FUNC_provider_query_operation_fn oqsprovider_query;
extern OSSL_FUNC_provider_get_capabilities_fn oqs_provider_get_capabilities;

/*
 * List of all algorithms with given OIDs
 */
///// OQS_TEMPLATE_FRAGMENT_ASSIGN_SIG_OIDS_START

#ifdef OQS_KEM_ENCODERS
#define OQS_OID_CNT 180
#else
#define OQS_OID_CNT 110
#endif
const char *oqs_oid_alg_list[OQS_OID_CNT] = {

#ifdef OQS_KEM_ENCODERS
    NULL,
    "frodo640aes",
    NULL,
    "p256_frodo640aes",
    NULL,
    "x25519_frodo640aes",
    NULL,
    "frodo640shake",
    NULL,
    "p256_frodo640shake",
    NULL,
    "x25519_frodo640shake",
    NULL,
    "frodo976aes",
    NULL,
    "p384_frodo976aes",
    NULL,
    "x448_frodo976aes",
    NULL,
    "frodo976shake",
    NULL,
    "p384_frodo976shake",
    NULL,
    "x448_frodo976shake",
    NULL,
    "frodo1344aes",
    NULL,
    "p521_frodo1344aes",
    NULL,
    "frodo1344shake",
    NULL,
    "p521_frodo1344shake",
    "2.16.840.1.101.3.4.4.1",
    "mlkem512",
    "1.3.6.1.4.1.22554.5.7.1",
    "p256_mlkem512",
    "1.3.6.1.4.1.22554.5.8.1",
    "x25519_mlkem512",
    "2.16.840.1.101.3.4.4.2",
    "mlkem768",
    NULL,
    "p384_mlkem768",
    NULL,
    "x448_mlkem768",
    NULL,
    "X25519MLKEM768",
    NULL,
    "SecP256r1MLKEM768",
    "2.16.840.1.101.3.4.4.3",
    "mlkem1024",
    NULL,
    "p521_mlkem1024",
    "1.3.6.1.4.1.42235.6",
    "SecP384r1MLKEM1024",
    NULL,
    "bikel1",
    NULL,
    "p256_bikel1",
    NULL,
    "x25519_bikel1",
    NULL,
    "bikel3",
    NULL,
    "p384_bikel3",
    NULL,
    "x448_bikel3",
    NULL,
    "bikel5",
    NULL,
    "p521_bikel5",

#endif /* OQS_KEM_ENCODERS */

    "2.16.840.1.101.3.4.3.17",
    "mldsa44",
    "1.3.9999.7.5",
    "p256_mldsa44",
    "1.3.9999.7.6",
    "rsa3072_mldsa44",
    "2.16.840.1.101.3.4.3.18",
    "mldsa65",
    "1.3.9999.7.7",
    "p384_mldsa65",
    "2.16.840.1.101.3.4.3.19",
    "mldsa87",
    "1.3.9999.7.8",
    "p521_mldsa87",
    "1.3.9999.3.11",
    "falcon512",
    "1.3.9999.3.12",
    "p256_falcon512",
    "1.3.9999.3.13",
    "rsa3072_falcon512",
    "1.3.9999.3.16",
    "falconpadded512",
    "1.3.9999.3.17",
    "p256_falconpadded512",
    "1.3.9999.3.18",
    "rsa3072_falconpadded512",
    "1.3.9999.3.14",
    "falcon1024",
    "1.3.9999.3.15",
    "p521_falcon1024",
    "1.3.9999.3.19",
    "falconpadded1024",
    "1.3.9999.3.20",
    "p521_falconpadded1024",
    "1.3.9999.6.4.13",
    "sphincssha2128fsimple",
    "1.3.9999.6.4.14",
    "p256_sphincssha2128fsimple",
    "1.3.9999.6.4.15",
    "rsa3072_sphincssha2128fsimple",
    "1.3.9999.6.4.16",
    "sphincssha2128ssimple",
    "1.3.9999.6.4.17",
    "p256_sphincssha2128ssimple",
    "1.3.9999.6.4.18",
    "rsa3072_sphincssha2128ssimple",
    "1.3.9999.6.5.10",
    "sphincssha2192fsimple",
    "1.3.9999.6.5.11",
    "p384_sphincssha2192fsimple",
    "1.3.9999.6.7.13",
    "sphincsshake128fsimple",
    "1.3.9999.6.7.14",
    "p256_sphincsshake128fsimple",
    "1.3.9999.6.7.15",
    "rsa3072_sphincsshake128fsimple",
    "1.3.9999.8.1.3",
    "mayo1",
    "1.3.9999.8.1.4",
    "p256_mayo1",
    "1.3.9999.8.2.3",
    "mayo2",
    "1.3.9999.8.2.4",
    "p256_mayo2",
    "1.3.9999.8.3.3",
    "mayo3",
    "1.3.9999.8.3.4",
    "p384_mayo3",
    "1.3.9999.8.5.3",
    "mayo5",
    "1.3.9999.8.5.4",
    "p521_mayo5",
    "1.3.6.1.4.1.62245.2.1.1.2",
    "CROSSrsdp128balanced",
    "1.3.9999.9.5.1",
    "OV_Is_pkc",
    "1.3.9999.9.5.2",
    "p256_OV_Is_pkc",
    "1.3.9999.9.6.1",
    "OV_Ip_pkc",
    "1.3.9999.9.6.2",
    "p256_OV_Ip_pkc",
    "1.3.9999.9.9.1",
    "OV_Is_pkc_skc",
    "1.3.9999.9.9.2",
    "p256_OV_Is_pkc_skc",
    "1.3.9999.9.10.1",
    "OV_Ip_pkc_skc",
    "1.3.9999.9.10.2",
    "p256_OV_Ip_pkc_skc",
    "1.3.9999.10.1.1",
    "snova2454",
    "1.3.9999.10.1.2",
    "p256_snova2454",
    "1.3.9999.10.3.1",
    "snova2454esk",
    "1.3.9999.10.3.2",
    "p256_snova2454esk",
    "1.3.9999.10.5.1",
    "snova37172",
    "1.3.9999.10.5.2",
    "p256_snova37172",
    "1.3.9999.10.10.1",
    "snova2455",
    "1.3.9999.10.10.2",
    "p384_snova2455",
    "1.3.9999.10.12.1",
    "snova2965",
    "1.3.9999.10.12.2",
    "p521_snova2965",
    ///// OQS_TEMPLATE_FRAGMENT_ASSIGN_SIG_OIDS_END
};

int oqs_patch_oids(void) {
    ///// OQS_TEMPLATE_FRAGMENT_OID_PATCHING_START
    {
        const char *envval = NULL;

#ifdef OQS_KEM_ENCODERS

        if ((envval = getenv("OQS_OID_FRODO640AES")))
            oqs_oid_alg_list[0] = envval;

        if ((envval = getenv("OQS_OID_P256_FRODO640AES")))
            oqs_oid_alg_list[2] = envval;
        if ((envval = getenv("OQS_OID_X25519_FRODO640AES")))
            oqs_oid_alg_list[4] = envval;
        if ((envval = getenv("OQS_OID_FRODO640SHAKE")))
            oqs_oid_alg_list[6] = envval;

        if ((envval = getenv("OQS_OID_P256_FRODO640SHAKE")))
            oqs_oid_alg_list[8] = envval;
        if ((envval = getenv("OQS_OID_X25519_FRODO640SHAKE")))
            oqs_oid_alg_list[10] = envval;
        if ((envval = getenv("OQS_OID_FRODO976AES")))
            oqs_oid_alg_list[12] = envval;

        if ((envval = getenv("OQS_OID_P384_FRODO976AES")))
            oqs_oid_alg_list[14] = envval;
        if ((envval = getenv("OQS_OID_X448_FRODO976AES")))
            oqs_oid_alg_list[16] = envval;
        if ((envval = getenv("OQS_OID_FRODO976SHAKE")))
            oqs_oid_alg_list[18] = envval;

        if ((envval = getenv("OQS_OID_P384_FRODO976SHAKE")))
            oqs_oid_alg_list[20] = envval;
        if ((envval = getenv("OQS_OID_X448_FRODO976SHAKE")))
            oqs_oid_alg_list[22] = envval;
        if ((envval = getenv("OQS_OID_FRODO1344AES")))
            oqs_oid_alg_list[24] = envval;

        if ((envval = getenv("OQS_OID_P521_FRODO1344AES")))
            oqs_oid_alg_list[26] = envval;
        if ((envval = getenv("OQS_OID_FRODO1344SHAKE")))
            oqs_oid_alg_list[28] = envval;

        if ((envval = getenv("OQS_OID_P521_FRODO1344SHAKE")))
            oqs_oid_alg_list[30] = envval;
        if ((envval = getenv("OQS_OID_MLKEM512")))
            oqs_oid_alg_list[32] = envval;

        if ((envval = getenv("OQS_OID_P256_MLKEM512")))
            oqs_oid_alg_list[34] = envval;
        if ((envval = getenv("OQS_OID_X25519_MLKEM512")))
            oqs_oid_alg_list[36] = envval;
        if ((envval = getenv("OQS_OID_MLKEM768")))
            oqs_oid_alg_list[38] = envval;

        if ((envval = getenv("OQS_OID_P384_MLKEM768")))
            oqs_oid_alg_list[40] = envval;
        if ((envval = getenv("OQS_OID_X448_MLKEM768")))
            oqs_oid_alg_list[42] = envval;
        if ((envval = getenv("OQS_OID_X25519MLKEM768")))
            oqs_oid_alg_list[44] = envval;
        if ((envval = getenv("OQS_OID_SECP256R1MLKEM768")))
            oqs_oid_alg_list[46] = envval;
        if ((envval = getenv("OQS_OID_MLKEM1024")))
            oqs_oid_alg_list[48] = envval;

        if ((envval = getenv("OQS_OID_P521_MLKEM1024")))
            oqs_oid_alg_list[50] = envval;
        if ((envval = getenv("OQS_OID_SECP384R1MLKEM1024")))
            oqs_oid_alg_list[52] = envval;
        if ((envval = getenv("OQS_OID_BIKEL1")))
            oqs_oid_alg_list[54] = envval;

        if ((envval = getenv("OQS_OID_P256_BIKEL1")))
            oqs_oid_alg_list[56] = envval;
        if ((envval = getenv("OQS_OID_X25519_BIKEL1")))
            oqs_oid_alg_list[58] = envval;
        if ((envval = getenv("OQS_OID_BIKEL3")))
            oqs_oid_alg_list[60] = envval;

        if ((envval = getenv("OQS_OID_P384_BIKEL3")))
            oqs_oid_alg_list[62] = envval;
        if ((envval = getenv("OQS_OID_X448_BIKEL3")))
            oqs_oid_alg_list[64] = envval;
        if ((envval = getenv("OQS_OID_BIKEL5")))
            oqs_oid_alg_list[66] = envval;

        if ((envval = getenv("OQS_OID_P521_BIKEL5")))
            oqs_oid_alg_list[68] = envval;

#define OQS_KEMOID_CNT 68 + 2
#else
#define OQS_KEMOID_CNT 0
#endif /* OQS_KEM_ENCODERS */
        if ((envval = getenv("OQS_OID_MLDSA44")))
            oqs_oid_alg_list[0 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_MLDSA44")))
            oqs_oid_alg_list[2 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_RSA3072_MLDSA44")))
            oqs_oid_alg_list[4 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_MLDSA65")))
            oqs_oid_alg_list[6 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P384_MLDSA65")))
            oqs_oid_alg_list[8 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_MLDSA87")))
            oqs_oid_alg_list[10 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P521_MLDSA87")))
            oqs_oid_alg_list[12 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_FALCON512")))
            oqs_oid_alg_list[14 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_FALCON512")))
            oqs_oid_alg_list[16 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_RSA3072_FALCON512")))
            oqs_oid_alg_list[18 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_FALCONPADDED512")))
            oqs_oid_alg_list[20 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_FALCONPADDED512")))
            oqs_oid_alg_list[22 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_RSA3072_FALCONPADDED512")))
            oqs_oid_alg_list[24 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_FALCON1024")))
            oqs_oid_alg_list[26 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P521_FALCON1024")))
            oqs_oid_alg_list[28 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_FALCONPADDED1024")))
            oqs_oid_alg_list[30 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P521_FALCONPADDED1024")))
            oqs_oid_alg_list[32 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SPHINCSSHA2128FSIMPLE")))
            oqs_oid_alg_list[34 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_SPHINCSSHA2128FSIMPLE")))
            oqs_oid_alg_list[36 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_RSA3072_SPHINCSSHA2128FSIMPLE")))
            oqs_oid_alg_list[38 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SPHINCSSHA2128SSIMPLE")))
            oqs_oid_alg_list[40 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_SPHINCSSHA2128SSIMPLE")))
            oqs_oid_alg_list[42 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_RSA3072_SPHINCSSHA2128SSIMPLE")))
            oqs_oid_alg_list[44 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SPHINCSSHA2192FSIMPLE")))
            oqs_oid_alg_list[46 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P384_SPHINCSSHA2192FSIMPLE")))
            oqs_oid_alg_list[48 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SPHINCSSHAKE128FSIMPLE")))
            oqs_oid_alg_list[50 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_SPHINCSSHAKE128FSIMPLE")))
            oqs_oid_alg_list[52 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_RSA3072_SPHINCSSHAKE128FSIMPLE")))
            oqs_oid_alg_list[54 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_MAYO1")))
            oqs_oid_alg_list[56 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_MAYO1")))
            oqs_oid_alg_list[58 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_MAYO2")))
            oqs_oid_alg_list[60 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_MAYO2")))
            oqs_oid_alg_list[62 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_MAYO3")))
            oqs_oid_alg_list[64 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P384_MAYO3")))
            oqs_oid_alg_list[66 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_MAYO5")))
            oqs_oid_alg_list[68 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P521_MAYO5")))
            oqs_oid_alg_list[70 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_CROSSRSDP128BALANCED")))
            oqs_oid_alg_list[72 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_OV_IS_PKC")))
            oqs_oid_alg_list[74 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_OV_IS_PKC")))
            oqs_oid_alg_list[76 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_OV_IP_PKC")))
            oqs_oid_alg_list[78 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_OV_IP_PKC")))
            oqs_oid_alg_list[80 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_OV_IS_PKC_SKC")))
            oqs_oid_alg_list[82 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_OV_IS_PKC_SKC")))
            oqs_oid_alg_list[84 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_OV_IP_PKC_SKC")))
            oqs_oid_alg_list[86 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_OV_IP_PKC_SKC")))
            oqs_oid_alg_list[88 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SNOVA2454")))
            oqs_oid_alg_list[90 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_SNOVA2454")))
            oqs_oid_alg_list[92 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SNOVA2454ESK")))
            oqs_oid_alg_list[94 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_SNOVA2454ESK")))
            oqs_oid_alg_list[96 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SNOVA37172")))
            oqs_oid_alg_list[98 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P256_SNOVA37172")))
            oqs_oid_alg_list[100 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SNOVA2455")))
            oqs_oid_alg_list[102 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P384_SNOVA2455")))
            oqs_oid_alg_list[104 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_SNOVA2965")))
            oqs_oid_alg_list[106 + OQS_KEMOID_CNT] = envval;
        if ((envval = getenv("OQS_OID_P521_SNOVA2965")))
            oqs_oid_alg_list[108 + OQS_KEMOID_CNT] = envval;
    } ///// OQS_TEMPLATE_FRAGMENT_OID_PATCHING_END
    return 1;
}

#define SIGALG(NAMES, SECBITS, FUNC)                                           \
    {                                                                          \
        NAMES, "provider=oqsprovider,oqsprovider.security_bits=" #SECBITS "",  \
            FUNC                                                               \
    }
#define KEMBASEALG(NAMES, SECBITS)                                             \
    {"" #NAMES "",                                                             \
     "provider=oqsprovider,oqsprovider.security_bits=" #SECBITS "",            \
     oqs_generic_kem_functions},

#define KEMHYBALG(NAMES, SECBITS)                                              \
    {"" #NAMES "",                                                             \
     "provider=oqsprovider,oqsprovider.security_bits=" #SECBITS "",            \
     oqs_hybrid_kem_functions},

#define KEMKMALG(NAMES, SECBITS)                                               \
    {"" #NAMES "",                                                             \
     "provider=oqsprovider,oqsprovider.security_bits=" #SECBITS "",            \
     oqs_##NAMES##_keymgmt_functions},

#define KEMKMHYBALG(NAMES, SECBITS, HYBTYPE)                                   \
    {"" #NAMES "",                                                             \
     "provider=oqsprovider,oqsprovider.security_bits=" #SECBITS "",            \
     oqs_##HYBTYPE##_##NAMES##_keymgmt_functions},

/* Functions provided by the core */
static OSSL_FUNC_core_gettable_params_fn *c_gettable_params = NULL;
static OSSL_FUNC_core_get_params_fn *c_get_params = NULL;

/* Parameters we provide to the core */
static const OSSL_PARAM oqsprovider_param_types[] = {
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_NAME, OSSL_PARAM_UTF8_PTR, NULL, 0),
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_VERSION, OSSL_PARAM_UTF8_PTR, NULL, 0),
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_BUILDINFO, OSSL_PARAM_UTF8_PTR, NULL, 0),
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_STATUS, OSSL_PARAM_INTEGER, NULL, 0),
    OSSL_PARAM_END};

static OSSL_ALGORITHM *oqsprovider_signatures_rt = NULL;

static const OSSL_ALGORITHM oqsprovider_signatures[] = {
///// OQS_TEMPLATE_FRAGMENT_SIG_FUNCTIONS_START
#ifdef OQS_ENABLE_SIG_ml_dsa_44
    SIGALG("mldsa44", 128, oqs_signature_functions),
    SIGALG("p256_mldsa44", 128, oqs_signature_functions),
    SIGALG("rsa3072_mldsa44", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_ml_dsa_65
    SIGALG("mldsa65", 192, oqs_signature_functions),
    SIGALG("p384_mldsa65", 192, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_ml_dsa_87
    SIGALG("mldsa87", 256, oqs_signature_functions),
    SIGALG("p521_mldsa87", 256, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_512
    SIGALG("falcon512", 128, oqs_signature_functions),
    SIGALG("p256_falcon512", 128, oqs_signature_functions),
    SIGALG("rsa3072_falcon512", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_padded_512
    SIGALG("falconpadded512", 128, oqs_signature_functions),
    SIGALG("p256_falconpadded512", 128, oqs_signature_functions),
    SIGALG("rsa3072_falconpadded512", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_1024
    SIGALG("falcon1024", 256, oqs_signature_functions),
    SIGALG("p521_falcon1024", 256, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_padded_1024
    SIGALG("falconpadded1024", 256, oqs_signature_functions),
    SIGALG("p521_falconpadded1024", 256, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha2_128f_simple
    SIGALG("sphincssha2128fsimple", 128, oqs_signature_functions),
    SIGALG("p256_sphincssha2128fsimple", 128, oqs_signature_functions),
    SIGALG("rsa3072_sphincssha2128fsimple", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha2_128s_simple
    SIGALG("sphincssha2128ssimple", 128, oqs_signature_functions),
    SIGALG("p256_sphincssha2128ssimple", 128, oqs_signature_functions),
    SIGALG("rsa3072_sphincssha2128ssimple", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha2_192f_simple
    SIGALG("sphincssha2192fsimple", 192, oqs_signature_functions),
    SIGALG("p384_sphincssha2192fsimple", 192, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_shake_128f_simple
    SIGALG("sphincsshake128fsimple", 128, oqs_signature_functions),
    SIGALG("p256_sphincsshake128fsimple", 128, oqs_signature_functions),
    SIGALG("rsa3072_sphincsshake128fsimple", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_1
    SIGALG("mayo1", 128, oqs_signature_functions),
    SIGALG("p256_mayo1", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_2
    SIGALG("mayo2", 128, oqs_signature_functions),
    SIGALG("p256_mayo2", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_3
    SIGALG("mayo3", 192, oqs_signature_functions),
    SIGALG("p384_mayo3", 192, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_5
    SIGALG("mayo5", 256, oqs_signature_functions),
    SIGALG("p521_mayo5", 256, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_cross_rsdp_128_balanced
    SIGALG("CROSSrsdp128balanced", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Is_pkc
    SIGALG("OV_Is_pkc", 128, oqs_signature_functions),
    SIGALG("p256_OV_Is_pkc", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Ip_pkc
    SIGALG("OV_Ip_pkc", 128, oqs_signature_functions),
    SIGALG("p256_OV_Ip_pkc", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Is_pkc_skc
    SIGALG("OV_Is_pkc_skc", 128, oqs_signature_functions),
    SIGALG("p256_OV_Is_pkc_skc", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Ip_pkc_skc
    SIGALG("OV_Ip_pkc_skc", 128, oqs_signature_functions),
    SIGALG("p256_OV_Ip_pkc_skc", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_24_5_4
    SIGALG("snova2454", 128, oqs_signature_functions),
    SIGALG("p256_snova2454", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_24_5_4_esk
    SIGALG("snova2454esk", 128, oqs_signature_functions),
    SIGALG("p256_snova2454esk", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_37_17_2
    SIGALG("snova37172", 128, oqs_signature_functions),
    SIGALG("p256_snova37172", 128, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_24_5_5
    SIGALG("snova2455", 192, oqs_signature_functions),
    SIGALG("p384_snova2455", 192, oqs_signature_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_29_6_5
    SIGALG("snova2965", 256, oqs_signature_functions),
    SIGALG("p521_snova2965", 256, oqs_signature_functions),
#endif
    ///// OQS_TEMPLATE_FRAGMENT_SIG_FUNCTIONS_END
    {NULL, NULL, NULL}};

static OSSL_ALGORITHM *oqsprovider_asym_kems_rt = NULL;
static const OSSL_ALGORITHM oqsprovider_asym_kems[] = {
///// OQS_TEMPLATE_FRAGMENT_KEM_FUNCTIONS_START
// clang-format off
#ifdef OQS_ENABLE_KEM_frodokem_640_aes
    KEMBASEALG(frodo640aes, 128)
    KEMHYBALG(p256_frodo640aes, 128)
    KEMHYBALG(x25519_frodo640aes, 128)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_640_shake
    KEMBASEALG(frodo640shake, 128)
    KEMHYBALG(p256_frodo640shake, 128)
    KEMHYBALG(x25519_frodo640shake, 128)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_976_aes
    KEMBASEALG(frodo976aes, 192)
    KEMHYBALG(p384_frodo976aes, 192)
    KEMHYBALG(x448_frodo976aes, 192)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_976_shake
    KEMBASEALG(frodo976shake, 192)
    KEMHYBALG(p384_frodo976shake, 192)
    KEMHYBALG(x448_frodo976shake, 192)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_1344_aes
    KEMBASEALG(frodo1344aes, 256)
    KEMHYBALG(p521_frodo1344aes, 256)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_1344_shake
    KEMBASEALG(frodo1344shake, 256)
    KEMHYBALG(p521_frodo1344shake, 256)
#endif
#ifdef OQS_ENABLE_KEM_ml_kem_512
    KEMBASEALG(mlkem512, 128)
    KEMHYBALG(p256_mlkem512, 128)
    KEMHYBALG(x25519_mlkem512, 128)
#endif
#ifdef OQS_ENABLE_KEM_ml_kem_768
    KEMBASEALG(mlkem768, 192)
    KEMHYBALG(p384_mlkem768, 192)
    KEMHYBALG(x448_mlkem768, 192)
    KEMHYBALG(X25519MLKEM768, 128)
    KEMHYBALG(SecP256r1MLKEM768, 128)
#endif
#ifdef OQS_ENABLE_KEM_ml_kem_1024
    KEMBASEALG(mlkem1024, 256)
    KEMHYBALG(p521_mlkem1024, 256)
    KEMHYBALG(SecP384r1MLKEM1024, 192)
#endif
#ifdef OQS_ENABLE_KEM_bike_l1
    KEMBASEALG(bikel1, 128)
    KEMHYBALG(p256_bikel1, 128)
    KEMHYBALG(x25519_bikel1, 128)
#endif
#ifdef OQS_ENABLE_KEM_bike_l3
    KEMBASEALG(bikel3, 192)
    KEMHYBALG(p384_bikel3, 192)
    KEMHYBALG(x448_bikel3, 192)
#endif
#ifdef OQS_ENABLE_KEM_bike_l5
    KEMBASEALG(bikel5, 256)
    KEMHYBALG(p521_bikel5, 256)
#endif
    // clang-format on
    ///// OQS_TEMPLATE_FRAGMENT_KEM_FUNCTIONS_END
    {NULL, NULL, NULL}};

static OSSL_ALGORITHM *oqsprovider_keymgmt_rt = NULL;
static const OSSL_ALGORITHM oqsprovider_keymgmt[] = {
///// OQS_TEMPLATE_FRAGMENT_KEYMGMT_FUNCTIONS_START
// clang-format off

#ifdef OQS_ENABLE_SIG_ml_dsa_44
    SIGALG("mldsa44", 128, oqs_mldsa44_keymgmt_functions),
    SIGALG("p256_mldsa44", 128, oqs_p256_mldsa44_keymgmt_functions),
    SIGALG("rsa3072_mldsa44", 128, oqs_rsa3072_mldsa44_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_ml_dsa_65
    SIGALG("mldsa65", 192, oqs_mldsa65_keymgmt_functions),
    SIGALG("p384_mldsa65", 192, oqs_p384_mldsa65_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_ml_dsa_87
    SIGALG("mldsa87", 256, oqs_mldsa87_keymgmt_functions),
    SIGALG("p521_mldsa87", 256, oqs_p521_mldsa87_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_512
    SIGALG("falcon512", 128, oqs_falcon512_keymgmt_functions),
    SIGALG("p256_falcon512", 128, oqs_p256_falcon512_keymgmt_functions),
    SIGALG("rsa3072_falcon512", 128, oqs_rsa3072_falcon512_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_padded_512
    SIGALG("falconpadded512", 128, oqs_falconpadded512_keymgmt_functions),
    SIGALG("p256_falconpadded512", 128, oqs_p256_falconpadded512_keymgmt_functions),
    SIGALG("rsa3072_falconpadded512", 128, oqs_rsa3072_falconpadded512_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_1024
    SIGALG("falcon1024", 256, oqs_falcon1024_keymgmt_functions),
    SIGALG("p521_falcon1024", 256, oqs_p521_falcon1024_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_falcon_padded_1024
    SIGALG("falconpadded1024", 256, oqs_falconpadded1024_keymgmt_functions),
    SIGALG("p521_falconpadded1024", 256, oqs_p521_falconpadded1024_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha2_128f_simple
    SIGALG("sphincssha2128fsimple", 128, oqs_sphincssha2128fsimple_keymgmt_functions),
    SIGALG("p256_sphincssha2128fsimple", 128, oqs_p256_sphincssha2128fsimple_keymgmt_functions),
    SIGALG("rsa3072_sphincssha2128fsimple", 128, oqs_rsa3072_sphincssha2128fsimple_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha2_128s_simple
    SIGALG("sphincssha2128ssimple", 128, oqs_sphincssha2128ssimple_keymgmt_functions),
    SIGALG("p256_sphincssha2128ssimple", 128, oqs_p256_sphincssha2128ssimple_keymgmt_functions),
    SIGALG("rsa3072_sphincssha2128ssimple", 128, oqs_rsa3072_sphincssha2128ssimple_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha2_192f_simple
    SIGALG("sphincssha2192fsimple", 192, oqs_sphincssha2192fsimple_keymgmt_functions),
    SIGALG("p384_sphincssha2192fsimple", 192, oqs_p384_sphincssha2192fsimple_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_shake_128f_simple
    SIGALG("sphincsshake128fsimple", 128, oqs_sphincsshake128fsimple_keymgmt_functions),
    SIGALG("p256_sphincsshake128fsimple", 128, oqs_p256_sphincsshake128fsimple_keymgmt_functions),
    SIGALG("rsa3072_sphincsshake128fsimple", 128, oqs_rsa3072_sphincsshake128fsimple_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_1
    SIGALG("mayo1", 128, oqs_mayo1_keymgmt_functions),
    SIGALG("p256_mayo1", 128, oqs_p256_mayo1_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_2
    SIGALG("mayo2", 128, oqs_mayo2_keymgmt_functions),
    SIGALG("p256_mayo2", 128, oqs_p256_mayo2_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_3
    SIGALG("mayo3", 192, oqs_mayo3_keymgmt_functions),
    SIGALG("p384_mayo3", 192, oqs_p384_mayo3_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_mayo_5
    SIGALG("mayo5", 256, oqs_mayo5_keymgmt_functions),
    SIGALG("p521_mayo5", 256, oqs_p521_mayo5_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_cross_rsdp_128_balanced
    SIGALG("CROSSrsdp128balanced", 128, oqs_CROSSrsdp128balanced_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Is_pkc
    SIGALG("OV_Is_pkc", 128, oqs_OV_Is_pkc_keymgmt_functions),
    SIGALG("p256_OV_Is_pkc", 128, oqs_p256_OV_Is_pkc_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Ip_pkc
    SIGALG("OV_Ip_pkc", 128, oqs_OV_Ip_pkc_keymgmt_functions),
    SIGALG("p256_OV_Ip_pkc", 128, oqs_p256_OV_Ip_pkc_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Is_pkc_skc
    SIGALG("OV_Is_pkc_skc", 128, oqs_OV_Is_pkc_skc_keymgmt_functions),
    SIGALG("p256_OV_Is_pkc_skc", 128, oqs_p256_OV_Is_pkc_skc_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_uov_ov_Ip_pkc_skc
    SIGALG("OV_Ip_pkc_skc", 128, oqs_OV_Ip_pkc_skc_keymgmt_functions),
    SIGALG("p256_OV_Ip_pkc_skc", 128, oqs_p256_OV_Ip_pkc_skc_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_24_5_4
    SIGALG("snova2454", 128, oqs_snova2454_keymgmt_functions),
    SIGALG("p256_snova2454", 128, oqs_p256_snova2454_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_24_5_4_esk
    SIGALG("snova2454esk", 128, oqs_snova2454esk_keymgmt_functions),
    SIGALG("p256_snova2454esk", 128, oqs_p256_snova2454esk_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_37_17_2
    SIGALG("snova37172", 128, oqs_snova37172_keymgmt_functions),
    SIGALG("p256_snova37172", 128, oqs_p256_snova37172_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_24_5_5
    SIGALG("snova2455", 192, oqs_snova2455_keymgmt_functions),
    SIGALG("p384_snova2455", 192, oqs_p384_snova2455_keymgmt_functions),
#endif
#ifdef OQS_ENABLE_SIG_snova_SNOVA_29_6_5
    SIGALG("snova2965", 256, oqs_snova2965_keymgmt_functions),
    SIGALG("p521_snova2965", 256, oqs_p521_snova2965_keymgmt_functions),
#endif

#ifdef OQS_ENABLE_KEM_frodokem_640_aes
    KEMKMALG(frodo640aes, 128)

    KEMKMHYBALG(p256_frodo640aes, 128, ecp)
    KEMKMHYBALG(x25519_frodo640aes, 128, ecx)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_640_shake
    KEMKMALG(frodo640shake, 128)

    KEMKMHYBALG(p256_frodo640shake, 128, ecp)
    KEMKMHYBALG(x25519_frodo640shake, 128, ecx)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_976_aes
    KEMKMALG(frodo976aes, 192)

    KEMKMHYBALG(p384_frodo976aes, 192, ecp)
    KEMKMHYBALG(x448_frodo976aes, 192, ecx)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_976_shake
    KEMKMALG(frodo976shake, 192)

    KEMKMHYBALG(p384_frodo976shake, 192, ecp)
    KEMKMHYBALG(x448_frodo976shake, 192, ecx)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_1344_aes
    KEMKMALG(frodo1344aes, 256)

    KEMKMHYBALG(p521_frodo1344aes, 256, ecp)
#endif
#ifdef OQS_ENABLE_KEM_frodokem_1344_shake
    KEMKMALG(frodo1344shake, 256)

    KEMKMHYBALG(p521_frodo1344shake, 256, ecp)
#endif
#ifdef OQS_ENABLE_KEM_ml_kem_512
    KEMKMALG(mlkem512, 128)

    KEMKMHYBALG(p256_mlkem512, 128, ecp)
    KEMKMHYBALG(x25519_mlkem512, 128, ecx)
#endif
#ifdef OQS_ENABLE_KEM_ml_kem_768
    KEMKMALG(mlkem768, 192)

    KEMKMHYBALG(p384_mlkem768, 192, ecp)
    KEMKMHYBALG(x448_mlkem768, 192, ecx)
    KEMKMHYBALG(X25519MLKEM768, 128, ecx)
    KEMKMHYBALG(SecP256r1MLKEM768, 128, ecp)
#endif
#ifdef OQS_ENABLE_KEM_ml_kem_1024
    KEMKMALG(mlkem1024, 256)

    KEMKMHYBALG(p521_mlkem1024, 256, ecp)
    KEMKMHYBALG(SecP384r1MLKEM1024, 192, ecp)
#endif
#ifdef OQS_ENABLE_KEM_bike_l1
    KEMKMALG(bikel1, 128)

    KEMKMHYBALG(p256_bikel1, 128, ecp)
    KEMKMHYBALG(x25519_bikel1, 128, ecx)
#endif
#ifdef OQS_ENABLE_KEM_bike_l3
    KEMKMALG(bikel3, 192)

    KEMKMHYBALG(p384_bikel3, 192, ecp)
    KEMKMHYBALG(x448_bikel3, 192, ecx)
#endif
#ifdef OQS_ENABLE_KEM_bike_l5
    KEMKMALG(bikel5, 256)

    KEMKMHYBALG(p521_bikel5, 256, ecp)
#endif
    // clang-format on
    ///// OQS_TEMPLATE_FRAGMENT_KEYMGMT_FUNCTIONS_END
    {NULL, NULL, NULL}};

static OSSL_ALGORITHM *oqsprovider_encoder_rt = NULL;
static const OSSL_ALGORITHM oqsprovider_encoder[] = {
#define ENCODER_PROVIDER "oqsprovider"
#include "oqsencoders.inc"
    {NULL, NULL, NULL}
#undef ENCODER_PROVIDER
};

static OSSL_ALGORITHM *oqsprovider_decoder_rt = NULL;
static const OSSL_ALGORITHM oqsprovider_decoder[] = {
#define DECODER_PROVIDER "oqsprovider"
#include "oqsdecoders.inc"
    {NULL, NULL, NULL}
#undef DECODER_PROVIDER
};

static const OSSL_PARAM *oqsprovider_gettable_params(void *provctx) {
    return oqsprovider_param_types;
}

#define OQS_PROVIDER_BASE_BUILD_INFO_STR                                       \
    "OQS Provider v." OQS_PROVIDER_VERSION_STR OQS_PROVIDER_COMMIT             \
    " based on liboqs v." OQS_VERSION_TEXT

#ifdef QSC_ENCODING_VERSION_STRING
#define OQS_PROVIDER_BUILD_INFO_STR                                            \
    OQS_PROVIDER_BASE_BUILD_INFO_STR                                           \
    " using qsc-key-encoder v." QSC_ENCODING_VERSION_STRING
#else
#define OQS_PROVIDER_BUILD_INFO_STR OQS_PROVIDER_BASE_BUILD_INFO_STR
#endif

static int oqsprovider_get_params(void *provctx, OSSL_PARAM params[]) {
    OSSL_PARAM *p;

    p = OSSL_PARAM_locate(params, OSSL_PROV_PARAM_NAME);
    if (p != NULL && !OSSL_PARAM_set_utf8_ptr(p, "OpenSSL OQS Provider"))
        return 0;
    p = OSSL_PARAM_locate(params, OSSL_PROV_PARAM_VERSION);
    if (p != NULL && !OSSL_PARAM_set_utf8_ptr(p, OQS_PROVIDER_VERSION_STR))
        return 0;
    p = OSSL_PARAM_locate(params, OSSL_PROV_PARAM_BUILDINFO);
    if (p != NULL && !OSSL_PARAM_set_utf8_ptr(p, OQS_PROVIDER_BUILD_INFO_STR))
        return 0;
    p = OSSL_PARAM_locate(params, OSSL_PROV_PARAM_STATUS);
    if (p != NULL && !OSSL_PARAM_set_int(p, 1)) // provider is always running
        return 0;
    // not passing in params to respond to is no error; response is empty then
    return 1;
}

int cnt_rt_disabled(const OSSL_ALGORITHM orig[], int len) {
    int dcnt = 0;

    for (int i = 0; i < len - 1; i++)
        if (sk_OPENSSL_STRING_find(rt_disabled_algs,
                                   (char *)orig[i].algorithm_names) >= 0)
            dcnt++;
    return dcnt;
}

#define FILTERED_ALGS(algs)                                                    \
    d_algs = cnt_rt_disabled(algs, OSSL_NELEM(algs));                          \
    if (algs##_rt == NULL) {                                                   \
        algs##_rt = OPENSSL_malloc(sizeof(OSSL_ALGORITHM) * OSSL_NELEM(algs) - \
                                   d_algs);                                    \
        n_cnt = 0;                                                             \
        for (int i = 0; i < OSSL_NELEM(algs); i++) {                           \
            if (sk_OPENSSL_STRING_find(rt_disabled_algs,                       \
                                       (char *)algs[i].algorithm_names) < 0) { \
                *(algs##_rt + n_cnt) = algs[i];                                \
                n_cnt++;                                                       \
            }                                                                  \
        }                                                                      \
    }                                                                          \
    return algs##_rt

static const OSSL_ALGORITHM *oqsprovider_query(void *provctx, int operation_id,
                                               int *no_cache) {
    int d_algs, n_cnt;
    *no_cache = 1;

    switch (operation_id) {
    case OSSL_OP_SIGNATURE:
        FILTERED_ALGS(oqsprovider_signatures);
    case OSSL_OP_KEM:
        FILTERED_ALGS(oqsprovider_asym_kems);
    case OSSL_OP_KEYMGMT:
        FILTERED_ALGS(oqsprovider_keymgmt);
    case OSSL_OP_ENCODER:
        FILTERED_ALGS(oqsprovider_encoder);
    case OSSL_OP_DECODER:
        FILTERED_ALGS(oqsprovider_decoder);
    default:
        if (getenv("OQSPROV"))
            printf("Unknown operation %d requested from OQS provider\n",
                   operation_id);
    }
    return NULL;
}

static void oqsprovider_teardown(void *provctx) {
    oqsx_freeprovctx((PROV_OQS_CTX *)provctx);
    OPENSSL_free(oqsprovider_signatures_rt);
    oqsprovider_signatures_rt = NULL;
    OPENSSL_free(oqsprovider_asym_kems_rt);
    oqsprovider_asym_kems_rt = NULL;
    OPENSSL_free(oqsprovider_keymgmt_rt);
    oqsprovider_keymgmt_rt = NULL;
    OPENSSL_free(oqsprovider_encoder_rt);
    oqsprovider_encoder_rt = NULL;
    OPENSSL_free(oqsprovider_decoder_rt);
    oqsprovider_decoder_rt = NULL;
    sk_OPENSSL_STRING_free(rt_disabled_algs);
    rt_disabled_algs = NULL;
    OQS_destroy();
}

/* Functions we provide to the core */
static const OSSL_DISPATCH oqsprovider_dispatch_table[] = {
    {OSSL_FUNC_PROVIDER_TEARDOWN, (void (*)(void))oqsprovider_teardown},
    {OSSL_FUNC_PROVIDER_GETTABLE_PARAMS,
     (void (*)(void))oqsprovider_gettable_params},
    {OSSL_FUNC_PROVIDER_GET_PARAMS, (void (*)(void))oqsprovider_get_params},
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))oqsprovider_query},
    {OSSL_FUNC_PROVIDER_GET_CAPABILITIES,
     (void (*)(void))oqs_provider_get_capabilities},
    {0, NULL}};

#ifdef OQS_PROVIDER_STATIC
#define OQS_PROVIDER_ENTRYPOINT_NAME oqs_provider_init
#else
#define OQS_PROVIDER_ENTRYPOINT_NAME OSSL_provider_init
#endif // ifdef OQS_PROVIDER_STATIC

static int algname_strcmp(const char *const *a, const char *const *b) {
    return strcmp(*a, *b);
}

int OQS_PROVIDER_ENTRYPOINT_NAME(const OSSL_CORE_HANDLE *handle,
                                 const OSSL_DISPATCH *in,
                                 const OSSL_DISPATCH **out, void **provctx) {
    const OSSL_DISPATCH *orig_in = in;
    OSSL_FUNC_core_obj_create_fn *c_obj_create = NULL;

    OSSL_FUNC_core_obj_add_sigid_fn *c_obj_add_sigid = NULL;
    BIO_METHOD *corebiometh;
    OSSL_LIB_CTX *libctx = NULL;
    int i, rc = 0;
    char *opensslv;
    const char *ossl_versionp = NULL;
    OSSL_PARAM version_request[] = {{"openssl-version", OSSL_PARAM_UTF8_PTR,
                                     &opensslv, sizeof(&opensslv), 0},
                                    {NULL, 0, NULL, 0, 0}};
    if (!rt_disabled_algs)
        rt_disabled_algs = sk_OPENSSL_STRING_new(algname_strcmp);

    OQS_init();

    if (!oqs_prov_bio_from_dispatch(in))
        goto end_init;

    if (!oqs_patch_codepoints())
        goto end_init;

    if (!oqs_patch_oids())
        goto end_init;

    for (; in->function_id != 0; in++) {
        switch (in->function_id) {
        case OSSL_FUNC_CORE_GETTABLE_PARAMS:
            c_gettable_params = OSSL_FUNC_core_gettable_params(in);
            break;
        case OSSL_FUNC_CORE_GET_PARAMS:
            c_get_params = OSSL_FUNC_core_get_params(in);
            break;
        case OSSL_FUNC_CORE_OBJ_CREATE:
            c_obj_create = OSSL_FUNC_core_obj_create(in);
            break;
        case OSSL_FUNC_CORE_OBJ_ADD_SIGID:
            c_obj_add_sigid = OSSL_FUNC_core_obj_add_sigid(in);
            break;
        /* Just ignore anything we don't understand */
        default:
            break;
        }
    }

    // we need these functions:
    if (c_obj_create == NULL || c_obj_add_sigid == NULL || c_get_params == NULL)
        goto end_init;

    // we need to know the version of the calling core to activate
    // suitable bug workarounds
    if (c_get_params(handle, version_request)) {
        ossl_versionp = *(void **)version_request[0].data;
    }

    // insert all OIDs to the global objects list
    for (i = 0; i < OQS_OID_CNT; i += 2) {
        int id_ok = 1;

        if (oqs_oid_alg_list[i] == NULL) {
            OQS_PROV_PRINTF2("OQS PROV: Warning: No OID registered for %s\n",
                             oqs_oid_alg_list[i + 1]);
        } else {
            if (!c_obj_create(handle, oqs_oid_alg_list[i],
                              oqs_oid_alg_list[i + 1],
                              oqs_oid_alg_list[i + 1])) {
                OQS_PROV_PRINTF2("error registering NID for %s\n",
                                 oqs_oid_alg_list[i + 1]);
                id_ok = 0;
                goto end_for;
            }

            /* create object (NID) again to avoid setup corner case problems
             * see https://github.com/openssl/openssl/discussions/21903
             * Not testing for errors is intentional.
             * At least one core version hangs up; so don't do this there:
             */
            if (strcmp("3.1.0", ossl_versionp)) {
                ERR_set_mark();
                OBJ_create(oqs_oid_alg_list[i], oqs_oid_alg_list[i + 1],
                           oqs_oid_alg_list[i + 1]);
                ERR_pop_to_mark();
            }

            if (!oqs_set_nid((char *)oqs_oid_alg_list[i + 1],
                             OBJ_sn2nid(oqs_oid_alg_list[i + 1]))) {
                ERR_raise(ERR_LIB_USER, OQSPROV_R_OBJ_CREATE_ERR);
                goto end_init;
            }

            if (!c_obj_add_sigid(handle, oqs_oid_alg_list[i + 1], "",
                                 oqs_oid_alg_list[i + 1])) {
                OQS_PROV_PRINTF2("error registering %s with no hash\n",
                                 oqs_oid_alg_list[i + 1]);
                id_ok = 0;
                goto end_for;
            }

            if (OBJ_sn2nid(oqs_oid_alg_list[i + 1]) != 0) {
                OQS_PROV_PRINTF3(
                    "OQS PROV: successfully registered %s with NID %d\n",
                    oqs_oid_alg_list[i + 1],
                    OBJ_sn2nid(oqs_oid_alg_list[i + 1]));
            } else {
                fprintf(stderr,
                        "OQS PROV: Impossible error: NID unregistered "
                        "for %s.\n",
                        oqs_oid_alg_list[i + 1]);
                ERR_raise(ERR_LIB_USER, OQSPROV_R_OBJ_CREATE_ERR);
                goto end_init;
            }
        end_for:
            if (!id_ok) {
                sk_OPENSSL_STRING_push(rt_disabled_algs,
                                       (char *)(oqs_oid_alg_list[i + 1]));
            }
        }
    }

    // ML-KEM implementation in OpenSSL 3.5 is _much_ more developed than this
    // code
    if (strcmp("3.5.0", ossl_versionp) <= 0) {
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mlkem512");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mlkem768");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "X25519MLKEM768");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "SecP256r1MLKEM768");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "SecP384r1MLKEM1024");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mlkem1024");
        // need to disable these as per
        // https://github.com/open-quantum-safe/oqs-provider/discussions/610#discussioncomment-12246359
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa44_pss2048");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa44_rsa2048");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa44_ed25519");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa44_p256");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa44_bp256");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa65_pss3072");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa65_rsa3072");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa65_p256");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa65_bp256");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa65_ed25519");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa87_p384");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa87_bp384");
        sk_OPENSSL_STRING_push(rt_disabled_algs, "mldsa87_ed448");
    }

    // output disabled algs:
    /*
    printf("disabled algs: %p (cnt: %d)\n", rt_disabled_algs,
    sk_OPENSSL_STRING_num(rt_disabled_algs));
    for (int i = 0; i < sk_OPENSSL_STRING_num(rt_disabled_algs); ++i) {
      printf("Disabled alg #%d: %s in OpenSSL version %s\n", i,
    sk_OPENSSL_STRING_value(rt_disabled_algs, i), ossl_versionp);
    }
    */

    // if libctx not yet existing, create a new one
    if (((corebiometh = oqs_bio_prov_init_bio_method()) == NULL) ||
        ((libctx = OSSL_LIB_CTX_new_child(handle, orig_in)) == NULL) ||
        ((*provctx = oqsx_newprovctx(libctx, handle, corebiometh)) == NULL)) {
        OQS_PROV_PRINTF("OQS PROV: error creating new provider context\n");
        ERR_raise(ERR_LIB_USER, OQSPROV_R_LIB_CREATE_ERR);
        goto end_init;
    }

    *out = oqsprovider_dispatch_table;

    // finally, warn if neither default nor fips provider are present:
    if (!OSSL_PROVIDER_available(libctx, "default") &&
        !OSSL_PROVIDER_available(libctx, "fips")) {
        OQS_PROV_PRINTF(
            "OQS PROV: Default and FIPS provider not available. Errors "
            "may result.\n");
    } else {
        OQS_PROV_PRINTF("OQS PROV: Default or FIPS provider available.\n");
    }
    rc = 1;

end_init:
    if (!rc) {
        if (ossl_versionp) {
            OQS_PROV_PRINTF2(
                "oqsprovider init failed for OpenSSL core version %s\n",
                ossl_versionp);
        } else
            OQS_PROV_PRINTF("oqsprovider init failed for OpenSSL\n");
        if (libctx)
            OSSL_LIB_CTX_free(libctx);
        if (provctx && *provctx) {
            oqsprovider_teardown(*provctx);
            *provctx = NULL;
        }
    }
    return rc;
}
