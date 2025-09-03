// SPDX-License-Identifier: Apache-2.0 AND MIT

/*
 * Main oqsprovider header file
 *
 * Code strongly inspired by OpenSSL crypto/ecx key handler.
 *
 */

/* Internal OQS functions for other submodules: not for application use */
#ifndef OQSX_H
#define OQSX_H

#ifndef OQS_PROVIDER_NOATOMIC
#include <stdatomic.h>
#endif

#include <openssl/bio.h>
#include <openssl/core.h>
#include <openssl/core_names.h>
#include <openssl/e_os2.h>
#include <openssl/opensslconf.h>

#define OQS_PROVIDER_VERSION_STR OQSPROVIDER_VERSION_TEXT

/* internal, but useful OSSL define */
#define OSSL_NELEM(x) (sizeof(x) / sizeof((x)[0]))

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

/* oqsprovider error codes */
#define OQSPROV_R_INVALID_DIGEST 1
#define OQSPROV_R_INVALID_SIZE 2
#define OQSPROV_R_INVALID_KEY 3
#define OQSPROV_R_UNSUPPORTED 4
#define OQSPROV_R_MISSING_OID 5
#define OQSPROV_R_OBJ_CREATE_ERR 6
#define OQSPROV_R_INVALID_ENCODING 7
#define OQSPROV_R_SIGN_ERROR 8
#define OQSPROV_R_LIB_CREATE_ERR 9
#define OQSPROV_R_NO_PRIVATE_KEY 10
#define OQSPROV_R_BUFFER_LENGTH_WRONG 11
#define OQSPROV_R_SIGNING_FAILED 12
#define OQSPROV_R_WRONG_PARAMETERS 13
#define OQSPROV_R_VERIFY_ERROR 14
#define OQSPROV_R_EVPINFO_MISSING 15
#define OQSPROV_R_INTERNAL_ERROR 16

/* Extra OpenSSL parameters for hybrid EVP_PKEY. */
#define OQS_HYBRID_PKEY_PARAM_CLASSICAL_PUB_KEY                                \
    "hybrid_classical_" OSSL_PKEY_PARAM_PUB_KEY
#define OQS_HYBRID_PKEY_PARAM_CLASSICAL_PRIV_KEY                               \
    "hybrid_classical_" OSSL_PKEY_PARAM_PRIV_KEY
#define OQS_HYBRID_PKEY_PARAM_PQ_PUB_KEY "hybrid_pq_" OSSL_PKEY_PARAM_PUB_KEY
#define OQS_HYBRID_PKEY_PARAM_PQ_PRIV_KEY "hybrid_pq_" OSSL_PKEY_PARAM_PRIV_KEY

STACK_OF(OPENSSL_STRING) * oqsprov_get_rt_disabled_algs();

/* Extras for OQS extension */

// clang-format off
// Helpers for (classic) key length storage
#define SIZE_OF_UINT32 4
#define ENCODE_UINT32(pbuf, i)                     \
    (pbuf)[0] = (unsigned char)((i >> 24) & 0xff); \
    (pbuf)[1] = (unsigned char)((i >> 16) & 0xff); \
    (pbuf)[2] = (unsigned char)((i >> 8) & 0xff);  \
    (pbuf)[3] = (unsigned char)((i) & 0xff)
#define DECODE_UINT32(i, pbuf)                         \
    i = ((uint32_t)((unsigned char *)pbuf)[0]) << 24;  \
    i |= ((uint32_t)((unsigned char *)pbuf)[1]) << 16; \
    i |= ((uint32_t)((unsigned char *)pbuf)[2]) << 8;  \
    i |= ((uint32_t)((unsigned char *)pbuf)[3])
// clang-format on

#define ON_ERR_SET_GOTO(condition, ret, code, gt)                              \
    if ((condition)) {                                                         \
        (ret) = (code);                                                        \
        goto gt;                                                               \
    }

#define ON_ERR_GOTO(condition, gt)                                             \
    if ((condition)) {                                                         \
        goto gt;                                                               \
    }

typedef struct prov_oqs_ctx_st {
    const OSSL_CORE_HANDLE *handle;
    OSSL_LIB_CTX *libctx; /* For all provider modules */
    BIO_METHOD *corebiometh;
} PROV_OQS_CTX;

PROV_OQS_CTX *oqsx_newprovctx(OSSL_LIB_CTX *libctx,
                              const OSSL_CORE_HANDLE *handle, BIO_METHOD *bm);
void oqsx_freeprovctx(PROV_OQS_CTX *ctx);
#define PROV_OQS_LIBCTX_OF(provctx)                                            \
    provctx ? (((PROV_OQS_CTX *)provctx)->libctx) : NULL

#include "oqs/oqs.h"

/* helper structure for classic key components in hybrid keys.
 * Actual tables in oqsprov_keys.c
 */
struct oqsx_evp_info_st {
    int keytype;
    int nid;
    int raw_key_support;
    size_t length_public_key;
    size_t length_private_key;
    size_t kex_length_secret;
    size_t length_signature;
};

typedef struct oqsx_evp_info_st OQSX_EVP_INFO;

struct oqsx_evp_ctx_st {
    EVP_PKEY_CTX *ctx;
    EVP_PKEY *keyParam;
    const OQSX_EVP_INFO *evp_info;
};

typedef struct oqsx_evp_ctx_st OQSX_EVP_CTX;

typedef union {
    OQS_SIG *sig;
    OQS_KEM *kem;
} OQSX_QS_CTX;

struct oqsx_provider_ctx_st {
    OQSX_QS_CTX oqsx_qs_ctx;
    OQSX_EVP_CTX *oqsx_evp_ctx;
};

typedef struct oqsx_provider_ctx_st OQSX_PROVIDER_CTX;

enum oqsx_key_type_en {
    KEY_TYPE_SIG,
    KEY_TYPE_KEM,
    KEY_TYPE_ECP_HYB_KEM,
    KEY_TYPE_ECX_HYB_KEM,
    KEY_TYPE_HYB_SIG
};

typedef enum oqsx_key_type_en OQSX_KEY_TYPE;

struct oqsx_key_st {
    OSSL_LIB_CTX *libctx;
#ifdef OQS_PROVIDER_NOATOMIC
    CRYPTO_RWLOCK *lock;
#endif
    char *propq;
    OQSX_KEY_TYPE keytype;
    OQSX_PROVIDER_CTX oqsx_provider_ctx;
    EVP_PKEY *classical_pkey; // for hybrid sigs
    const OQSX_EVP_INFO *evp_info;
    size_t numkeys;

    /* Indicates if the share of a hybrid scheme should be reversed */
    int reverse_share;

    /* key lengths including size fields for classic key length information:
     * (numkeys-1)*SIZE_OF_UINT32
     */
    size_t privkeylen;
    size_t pubkeylen;
    size_t bit_security;
    char *tls_name;
#ifndef OQS_PROVIDER_NOATOMIC
    _Atomic
#endif
        int references;
    /* point to actual priv key material -- classic key, if present, first
     * i.e., OQS key always at comp_*key[numkeys-1]
     */
    void **comp_privkey;
    void **comp_pubkey;

    /* contain key material: First SIZE_OF_UINT32 bytes indicating actual
     * classic key length in case of hybrid keys (if numkeys>1)
     */
    void *privkey;
    void *pubkey;
};

typedef struct oqsx_key_st OQSX_KEY;

/* Register given NID with tlsname in OSSL3 registry */
int oqs_set_nid(char *tlsname, int nid);

/* Create OQSX_KEY data structure based on parameters; key material allocated
 * separately */
OQSX_KEY *oqsx_key_new(OSSL_LIB_CTX *libctx, char *oqs_name, char *tls_name,
                       int is_kem, const char *propq, int bit_security,
                       int alg_idx, int reverse_share);

/* allocate key material; component pointers need to be set separately */
int oqsx_key_allocate_keymaterial(OQSX_KEY *key, int include_private);

/* free all data structures, incl. key material */
void oqsx_key_free(OQSX_KEY *key);

/* increase reference count of given key */
int oqsx_key_up_ref(OQSX_KEY *key);

/* do (composite) key generation */
int oqsx_key_gen(OQSX_KEY *key);

/* create OQSX_KEY from pkcs8 data structure */
OQSX_KEY *oqsx_key_from_pkcs8(const PKCS8_PRIV_KEY_INFO *p8inf,
                              OSSL_LIB_CTX *libctx, const char *propq);

/* create OQSX_KEY (public key material only) from X509 data structure */
OQSX_KEY *oqsx_key_from_x509pubkey(const X509_PUBKEY *xpk, OSSL_LIB_CTX *libctx,
                                   const char *propq);

/* Backend support */
/* populate key material from parameters */
int oqsx_key_fromdata(OQSX_KEY *oqsxk, const OSSL_PARAM params[],
                      int include_private);
/* retrieve security bit count for key */
int oqsx_key_secbits(OQSX_KEY *k);
/* retrieve pure OQS key len */
int oqsx_key_get_oqs_public_key_len(OQSX_KEY *k);
/* retrieve maximum size of generated artifact (shared secret or signature,
 * respectively) */
int oqsx_key_maxsize(OQSX_KEY *k);
void oqsx_key_set0_libctx(OQSX_KEY *key, OSSL_LIB_CTX *libctx);
int oqs_patch_codepoints(void);

/* Function prototypes */

extern const OSSL_DISPATCH oqs_generic_kem_functions[];
extern const OSSL_DISPATCH oqs_hybrid_kem_functions[];
extern const OSSL_DISPATCH oqs_signature_functions[];

///// OQS_TEMPLATE_FRAGMENT_ENDECODER_FUNCTIONS_START
#ifdef OQS_KEM_ENCODERS

extern const OSSL_DISPATCH
    oqs_frodo640aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_frodo640aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_frodo640aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_frodo640aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_frodo640aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_frodo640aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_frodo640aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x25519_frodo640aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x25519_frodo640aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x25519_frodo640aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo640shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_frodo640shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_frodo640shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_frodo640shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_frodo640shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_frodo640shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_frodo640shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_frodo640shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_frodo640shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x25519_frodo640shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x25519_frodo640shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x25519_frodo640shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_frodo976aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_frodo976aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_frodo976aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_frodo976aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_frodo976aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_frodo976aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x448_frodo976aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x448_frodo976aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x448_frodo976aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo976shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_frodo976shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_frodo976shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_frodo976shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_frodo976shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_frodo976shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_frodo976shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_frodo976shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_frodo976shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x448_frodo976shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x448_frodo976shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x448_frodo976shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_frodo1344aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_frodo1344aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_frodo1344aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344aes_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344aes_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344aes_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344aes_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344aes_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344aes_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_frodo1344aes_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_frodo1344aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_frodo1344aes_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_frodo1344shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_frodo1344shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_frodo1344shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_frodo1344shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344shake_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344shake_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344shake_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344shake_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344shake_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_frodo1344shake_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_frodo1344shake_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_frodo1344shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_frodo1344shake_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mlkem512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_mlkem512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mlkem512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mlkem512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mlkem512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mlkem512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mlkem512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mlkem512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mlkem512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_mlkem512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_mlkem512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_mlkem512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_mlkem512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_mlkem512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_mlkem512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_mlkem512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_mlkem512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_mlkem512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x25519_mlkem512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x25519_mlkem512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x25519_mlkem512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem768_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem768_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem768_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem768_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem768_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem768_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mlkem768_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_mlkem768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mlkem768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mlkem768_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mlkem768_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mlkem768_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mlkem768_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mlkem768_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mlkem768_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_mlkem768_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_mlkem768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_mlkem768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_mlkem768_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_mlkem768_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_mlkem768_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_mlkem768_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_mlkem768_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_mlkem768_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x448_mlkem768_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x448_mlkem768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x448_mlkem768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_X25519MLKEM768_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_X25519MLKEM768_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_X25519MLKEM768_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_X25519MLKEM768_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_X25519MLKEM768_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_X25519MLKEM768_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_X25519MLKEM768_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_X25519MLKEM768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_X25519MLKEM768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP256r1MLKEM768_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP256r1MLKEM768_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP256r1MLKEM768_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP256r1MLKEM768_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP256r1MLKEM768_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP256r1MLKEM768_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_SecP256r1MLKEM768_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_SecP256r1MLKEM768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_SecP256r1MLKEM768_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem1024_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mlkem1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mlkem1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_mlkem1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mlkem1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mlkem1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mlkem1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mlkem1024_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mlkem1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mlkem1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mlkem1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_mlkem1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_mlkem1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_mlkem1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP384r1MLKEM1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP384r1MLKEM1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP384r1MLKEM1024_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP384r1MLKEM1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP384r1MLKEM1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_SecP384r1MLKEM1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_SecP384r1MLKEM1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_SecP384r1MLKEM1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_SecP384r1MLKEM1024_decoder_functions[];
extern const OSSL_DISPATCH oqs_bikel1_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_bikel1_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel1_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel1_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel1_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel1_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_bikel1_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_bikel1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_bikel1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_bikel1_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_bikel1_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_bikel1_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_bikel1_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_bikel1_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_bikel1_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_bikel1_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_bikel1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_bikel1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_bikel1_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_bikel1_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_bikel1_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_bikel1_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_bikel1_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x25519_bikel1_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x25519_bikel1_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x25519_bikel1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x25519_bikel1_decoder_functions[];
extern const OSSL_DISPATCH oqs_bikel3_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_bikel3_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel3_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel3_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel3_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel3_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_bikel3_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_bikel3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_bikel3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_bikel3_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_bikel3_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_bikel3_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_bikel3_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_bikel3_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_bikel3_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_bikel3_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_bikel3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_bikel3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_bikel3_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_bikel3_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_bikel3_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_bikel3_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_bikel3_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_x448_bikel3_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_x448_bikel3_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_x448_bikel3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_x448_bikel3_decoder_functions[];
extern const OSSL_DISPATCH oqs_bikel5_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_bikel5_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel5_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel5_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel5_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_bikel5_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_bikel5_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_bikel5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_bikel5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_bikel5_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_bikel5_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_bikel5_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_bikel5_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_bikel5_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_bikel5_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_bikel5_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_bikel5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_bikel5_decoder_functions[];

#endif /* OQS_KEM_ENCODERS */

extern const OSSL_DISPATCH
    oqs_mldsa44_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa44_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa44_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa44_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa44_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa44_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mldsa44_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_mldsa44_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mldsa44_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mldsa44_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mldsa44_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mldsa44_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mldsa44_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mldsa44_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mldsa44_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_mldsa44_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_mldsa44_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_mldsa44_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_mldsa44_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_mldsa44_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_mldsa44_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_mldsa44_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_mldsa44_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_mldsa44_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_rsa3072_mldsa44_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_rsa3072_mldsa44_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_rsa3072_mldsa44_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa65_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa65_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa65_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa65_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa65_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa65_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mldsa65_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_mldsa65_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mldsa65_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mldsa65_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mldsa65_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mldsa65_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mldsa65_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mldsa65_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mldsa65_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_mldsa65_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_mldsa65_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_mldsa65_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa87_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa87_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa87_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa87_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa87_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mldsa87_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mldsa87_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_mldsa87_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mldsa87_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mldsa87_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mldsa87_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mldsa87_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mldsa87_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mldsa87_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mldsa87_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_mldsa87_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_mldsa87_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_mldsa87_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_falcon512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_falcon512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_falcon512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falcon512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falcon512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falcon512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falcon512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falcon512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falcon512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_falcon512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_falcon512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_falcon512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falcon512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falcon512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falcon512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falcon512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falcon512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falcon512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_rsa3072_falcon512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_rsa3072_falcon512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_rsa3072_falcon512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_falconpadded512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_falconpadded512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_falconpadded512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falconpadded512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falconpadded512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falconpadded512_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falconpadded512_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falconpadded512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_falconpadded512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_falconpadded512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_falconpadded512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_falconpadded512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_falconpadded512_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_rsa3072_falconpadded512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_rsa3072_falconpadded512_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon1024_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falcon1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_falcon1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_falcon1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_falcon1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falcon1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falcon1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falcon1024_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falcon1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falcon1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falcon1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_falcon1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_falcon1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_falcon1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded1024_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_falconpadded1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_falconpadded1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_falconpadded1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_falconpadded1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_falconpadded1024_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_falconpadded1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_falconpadded1024_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_sphincssha2128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_sphincssha2128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_sphincssha2128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_sphincssha2128fsimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_rsa3072_sphincssha2128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_rsa3072_sphincssha2128fsimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2128ssimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_sphincssha2128ssimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_sphincssha2128ssimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincssha2128ssimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_sphincssha2128ssimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_sphincssha2128ssimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_rsa3072_sphincssha2128ssimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_rsa3072_sphincssha2128ssimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincssha2192fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_sphincssha2192fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_sphincssha2192fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p384_sphincssha2192fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_sphincssha2192fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_sphincssha2192fsimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_sphincsshake128fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_sphincsshake128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_sphincsshake128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_p256_sphincsshake128fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_sphincsshake128fsimple_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_sphincsshake128fsimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_PrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_PrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_EncryptedPrivateKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_EncryptedPrivateKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_SubjectPublicKeyInfo_der_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_SubjectPublicKeyInfo_pem_encoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_rsa3072_sphincsshake128fsimple_decoder_functions
        [];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_rsa3072_sphincsshake128fsimple_decoder_functions
        [];
extern const OSSL_DISPATCH oqs_mayo1_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo1_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo1_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo1_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo1_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo1_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo1_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_mayo1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mayo1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo1_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo1_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo1_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo1_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo1_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo1_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_mayo1_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_mayo1_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_mayo1_decoder_functions[];
extern const OSSL_DISPATCH oqs_mayo2_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo2_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo2_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo2_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo2_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo2_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo2_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_mayo2_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mayo2_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo2_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo2_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo2_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo2_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo2_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_mayo2_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_mayo2_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_mayo2_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_mayo2_decoder_functions[];
extern const OSSL_DISPATCH oqs_mayo3_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo3_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo3_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo3_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo3_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo3_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo3_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_mayo3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mayo3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mayo3_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mayo3_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mayo3_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mayo3_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mayo3_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_mayo3_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_mayo3_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_mayo3_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_mayo3_decoder_functions[];
extern const OSSL_DISPATCH oqs_mayo5_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo5_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo5_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo5_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo5_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_mayo5_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_mayo5_to_text_encoder_functions[];
extern const OSSL_DISPATCH oqs_PrivateKeyInfo_der_to_mayo5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_mayo5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mayo5_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mayo5_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mayo5_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mayo5_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mayo5_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_mayo5_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_mayo5_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_mayo5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_mayo5_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_CROSSrsdp128balanced_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_CROSSrsdp128balanced_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_CROSSrsdp128balanced_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_CROSSrsdp128balanced_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_CROSSrsdp128balanced_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_CROSSrsdp128balanced_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_CROSSrsdp128balanced_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_CROSSrsdp128balanced_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_CROSSrsdp128balanced_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_OV_Is_pkc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_OV_Is_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_OV_Is_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Is_pkc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_OV_Is_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_OV_Is_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_OV_Ip_pkc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_OV_Ip_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_OV_Ip_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Ip_pkc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_OV_Ip_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_OV_Ip_pkc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_skc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_skc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_skc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_skc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_skc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Is_pkc_skc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_OV_Is_pkc_skc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_OV_Is_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_OV_Is_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_skc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_skc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_skc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_skc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_skc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Is_pkc_skc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Is_pkc_skc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_OV_Is_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_OV_Is_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_skc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_skc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_skc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_skc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_skc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_OV_Ip_pkc_skc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_OV_Ip_pkc_skc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_OV_Ip_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_OV_Ip_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_skc_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_skc_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_skc_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_skc_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_skc_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_OV_Ip_pkc_skc_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Ip_pkc_skc_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_OV_Ip_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_OV_Ip_pkc_skc_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_snova2454_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_snova2454_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_snova2454_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_snova2454_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_snova2454_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_snova2454_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454esk_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454esk_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454esk_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454esk_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454esk_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2454esk_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_snova2454esk_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_snova2454esk_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_snova2454esk_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454esk_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454esk_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454esk_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454esk_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454esk_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova2454esk_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_snova2454esk_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_snova2454esk_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_snova2454esk_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova37172_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova37172_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova37172_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova37172_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova37172_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova37172_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_snova37172_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_snova37172_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_snova37172_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova37172_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova37172_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova37172_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova37172_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova37172_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p256_snova37172_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p256_snova37172_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p256_snova37172_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p256_snova37172_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2455_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2455_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2455_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2455_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2455_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2455_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_snova2455_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_snova2455_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_snova2455_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_snova2455_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_snova2455_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_snova2455_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_snova2455_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_snova2455_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p384_snova2455_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p384_snova2455_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p384_snova2455_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p384_snova2455_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2965_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2965_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2965_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2965_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2965_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_snova2965_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_snova2965_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_snova2965_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_snova2965_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_snova2965_to_PrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_snova2965_to_PrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_snova2965_to_EncryptedPrivateKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_snova2965_to_EncryptedPrivateKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_snova2965_to_SubjectPublicKeyInfo_der_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_p521_snova2965_to_SubjectPublicKeyInfo_pem_encoder_functions[];
extern const OSSL_DISPATCH oqs_p521_snova2965_to_text_encoder_functions[];
extern const OSSL_DISPATCH
    oqs_PrivateKeyInfo_der_to_p521_snova2965_decoder_functions[];
extern const OSSL_DISPATCH
    oqs_SubjectPublicKeyInfo_der_to_p521_snova2965_decoder_functions[];
///// OQS_TEMPLATE_FRAGMENT_ENDECODER_FUNCTIONS_END

///// OQS_TEMPLATE_FRAGMENT_ALG_FUNCTIONS_START
extern const OSSL_DISPATCH oqs_mldsa44_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_mldsa44_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_rsa3072_mldsa44_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mldsa65_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p384_mldsa65_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mldsa87_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p521_mldsa87_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_falcon512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_falcon512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_rsa3072_falcon512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_falconpadded512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_falconpadded512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_rsa3072_falconpadded512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_falcon1024_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p521_falcon1024_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_falconpadded1024_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p521_falconpadded1024_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_sphincssha2128fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_sphincssha2128fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_sphincssha2128ssimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_sphincssha2128ssimple_keymgmt_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincssha2128ssimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_sphincssha2192fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p384_sphincssha2192fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_sphincsshake128fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_sphincsshake128fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH
    oqs_rsa3072_sphincsshake128fsimple_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mayo1_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_mayo1_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mayo2_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_mayo2_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mayo3_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p384_mayo3_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mayo5_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p521_mayo5_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_CROSSrsdp128balanced_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_OV_Is_pkc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Is_pkc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_OV_Ip_pkc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Ip_pkc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_OV_Is_pkc_skc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Is_pkc_skc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_OV_Ip_pkc_skc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_OV_Ip_pkc_skc_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_snova2454_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_snova2454_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_snova2454esk_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_snova2454esk_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_snova37172_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p256_snova37172_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_snova2455_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p384_snova2455_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_snova2965_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_p521_snova2965_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_frodo640aes_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p256_frodo640aes_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x25519_frodo640aes_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_frodo640shake_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p256_frodo640shake_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x25519_frodo640shake_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_frodo976aes_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p384_frodo976aes_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x448_frodo976aes_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_frodo976shake_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p384_frodo976shake_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x448_frodo976shake_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_frodo1344aes_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p521_frodo1344aes_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_frodo1344shake_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p521_frodo1344shake_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mlkem512_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p256_mlkem512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x25519_mlkem512_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mlkem768_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p384_mlkem768_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x448_mlkem768_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_X25519MLKEM768_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecp_SecP256r1MLKEM768_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_mlkem1024_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p521_mlkem1024_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecp_SecP384r1MLKEM1024_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_bikel1_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p256_bikel1_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x25519_bikel1_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_bikel3_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p384_bikel3_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_ecx_x448_bikel3_keymgmt_functions[];
extern const OSSL_DISPATCH oqs_bikel5_keymgmt_functions[];

extern const OSSL_DISPATCH oqs_ecp_p521_bikel5_keymgmt_functions[];
///// OQS_TEMPLATE_FRAGMENT_ALG_FUNCTIONS_END

/* BIO function declarations */
int oqs_prov_bio_from_dispatch(const OSSL_DISPATCH *fns);

OSSL_CORE_BIO *oqs_prov_bio_new_file(const char *filename, const char *mode);
OSSL_CORE_BIO *oqs_prov_bio_new_membuf(const char *filename, int len);
int oqs_prov_bio_read_ex(OSSL_CORE_BIO *bio, void *data, size_t data_len,
                         size_t *bytes_read);
int oqs_prov_bio_write_ex(OSSL_CORE_BIO *bio, const void *data, size_t data_len,
                          size_t *written);
int oqs_prov_bio_gets(OSSL_CORE_BIO *bio, char *buf, int size);
int oqs_prov_bio_puts(OSSL_CORE_BIO *bio, const char *str);
int oqs_prov_bio_ctrl(OSSL_CORE_BIO *bio, int cmd, long num, void *ptr);
int oqs_prov_bio_up_ref(OSSL_CORE_BIO *bio);
int oqs_prov_bio_free(OSSL_CORE_BIO *bio);
int oqs_prov_bio_vprintf(OSSL_CORE_BIO *bio, const char *format, va_list ap);
int oqs_prov_bio_printf(OSSL_CORE_BIO *bio, const char *format, ...);

BIO_METHOD *oqs_bio_prov_init_bio_method(void);
BIO *oqs_bio_new_from_core_bio(PROV_OQS_CTX *provctx, OSSL_CORE_BIO *corebio);

#endif
