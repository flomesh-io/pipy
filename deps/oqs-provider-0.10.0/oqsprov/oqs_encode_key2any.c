// SPDX-License-Identifier: Apache-2.0 AND MIT

/*
 * OQS OpenSSL 3 provider
 *
 * Code strongly inspired by OpenSSL endecoder.
 *
 * ToDo: Adding hybrid alg support
 */

#include <openssl/asn1.h>
#include <openssl/core.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h> /* PKCS8_encrypt() */
#include <openssl/proverr.h>
#include <openssl/x509.h>
#include <string.h>

#include "oqs_endecoder_local.h"

#ifdef NDEBUG
#define OQS_ENC_PRINTF(a)
#define OQS_ENC_PRINTF2(a, b)
#define OQS_ENC_PRINTF3(a, b, c)
#else
#define OQS_ENC_PRINTF(a)                                                      \
    if (getenv("OQSENC"))                                                      \
    printf(a)
#define OQS_ENC_PRINTF2(a, b)                                                  \
    if (getenv("OQSENC"))                                                      \
    printf(a, b)
#define OQS_ENC_PRINTF3(a, b, c)                                               \
    if (getenv("OQSENC"))                                                      \
    printf(a, b, c)
#endif // NDEBUG

struct key2any_ctx_st {
    PROV_OQS_CTX *provctx;

    /* Set to 0 if parameters should not be saved (dsa only) */
    int save_parameters;

    /* Set to 1 if intending to encrypt/decrypt, otherwise 0 */
    int cipher_intent;

    EVP_CIPHER *cipher;

    OSSL_PASSPHRASE_CALLBACK *pwcb;
    void *pwcbarg;
};

typedef int check_key_type_fn(const void *key, int nid);
typedef int key_to_paramstring_fn(const void *key, int nid, int save,
                                  void **str, int *strtype);
typedef int key_to_der_fn(BIO *out, const void *key, int key_nid,
                          const char *pemname, key_to_paramstring_fn *p2s,
                          i2d_of_void *k2d, struct key2any_ctx_st *ctx);
typedef int write_bio_of_void_fn(BIO *bp, const void *x);

/* Free the blob allocated during key_to_paramstring_fn */
static void free_asn1_data(int type, void *data) {
    switch (type) {
    case V_ASN1_OBJECT:
        ASN1_OBJECT_free(data);
        break;
    case V_ASN1_SEQUENCE:
        ASN1_STRING_free(data);
        break;
    }
}

static PKCS8_PRIV_KEY_INFO *key_to_p8info(const void *key, int key_nid,
                                          void *params, int params_type,
                                          i2d_of_void *k2d) {
    /* der, derlen store the key DER output and its length */
    unsigned char *der = NULL;
    int derlen;
    /* The final PKCS#8 info */
    PKCS8_PRIV_KEY_INFO *p8info = NULL;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_p8info called\n");

    if ((p8info = PKCS8_PRIV_KEY_INFO_new()) == NULL ||
        (derlen = k2d(key, &der)) <= 0 ||
        !PKCS8_pkey_set0(p8info, OBJ_nid2obj(key_nid), 0,
                         // doesn't work with oqs-openssl:
                         //  params_type, params,
                         // does work/interop:
                         V_ASN1_UNDEF, NULL, der, derlen)) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        PKCS8_PRIV_KEY_INFO_free(p8info);
        OPENSSL_free(der);
        p8info = NULL;
    }

    return p8info;
}

static X509_SIG *p8info_to_encp8(PKCS8_PRIV_KEY_INFO *p8info,
                                 struct key2any_ctx_st *ctx) {
    X509_SIG *p8 = NULL;
    char kstr[PEM_BUFSIZE];
    size_t klen = 0;
    OSSL_LIB_CTX *libctx = PROV_OQS_LIBCTX_OF(ctx->provctx);

    OQS_ENC_PRINTF("OQS ENC provider: p8info_to_encp8 called\n");

    if (ctx->cipher == NULL || ctx->pwcb == NULL)
        return NULL;

    if (!ctx->pwcb(kstr, PEM_BUFSIZE, &klen, NULL, ctx->pwcbarg)) {
        ERR_raise(ERR_LIB_USER, PROV_R_UNABLE_TO_GET_PASSPHRASE);
        return NULL;
    }
    /* First argument == -1 means "standard" */
    p8 = PKCS8_encrypt_ex(-1, ctx->cipher, kstr, klen, NULL, 0, 0, p8info,
                          libctx, NULL);
    OPENSSL_cleanse(kstr, klen);
    return p8;
}

static X509_SIG *key_to_encp8(const void *key, int key_nid, void *params,
                              int params_type, i2d_of_void *k2d,
                              struct key2any_ctx_st *ctx) {
    PKCS8_PRIV_KEY_INFO *p8info =
        key_to_p8info(key, key_nid, params, params_type, k2d);
    X509_SIG *p8 = NULL;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_encp8 called\n");

    if (p8info == NULL) {
        free_asn1_data(params_type, params);
    } else {
        p8 = p8info_to_encp8(p8info, ctx);
        PKCS8_PRIV_KEY_INFO_free(p8info);
    }
    return p8;
}

static X509_PUBKEY *oqsx_key_to_pubkey(const void *key, int key_nid,
                                       void *params, int params_type,
                                       i2d_of_void k2d) {
    /* der, derlen store the key DER output and its length */
    unsigned char *der = NULL;
    int derlen;
    /* The final X509_PUBKEY */
    X509_PUBKEY *xpk = NULL;

    OQS_ENC_PRINTF2("OQS ENC provider: oqsx_key_to_pubkey called for NID %d\n",
                    key_nid);

    if ((xpk = X509_PUBKEY_new()) == NULL || (derlen = k2d(key, &der)) <= 0 ||
        !X509_PUBKEY_set0_param(
            xpk, OBJ_nid2obj(key_nid), V_ASN1_UNDEF,
            NULL, // as per logic in oqs_meth.c in oqs-openssl
            der, derlen)) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        X509_PUBKEY_free(xpk);
        OPENSSL_free(der);
        xpk = NULL;
    }

    return xpk;
}

/*
 * key_to_epki_* produce encoded output with the private key data in a
 * EncryptedPrivateKeyInfo structure (defined by PKCS#8).  They require
 * that there's an intent to encrypt, anything else is an error.
 *
 * key_to_pki_* primarly produce encoded output with the private key data
 * in a PrivateKeyInfo structure (also defined by PKCS#8).  However, if
 * there is an intent to encrypt the data, the corresponding key_to_epki_*
 * function is used instead.
 *
 * key_to_spki_* produce encoded output with the public key data in an
 * X.509 SubjectPublicKeyInfo.
 *
 * Key parameters don't have any defined envelopment of this kind, but are
 * included in some manner in the output from the functions described above,
 * either in the AlgorithmIdentifier's parameter field, or as part of the
 * key data itself.
 */

static int key_to_epki_der_priv_bio(BIO *out, const void *key, int key_nid,
                                    ossl_unused const char *pemname,
                                    key_to_paramstring_fn *p2s,
                                    i2d_of_void *k2d,
                                    struct key2any_ctx_st *ctx) {
    int ret = 0;
    void *str = NULL;
    int strtype = V_ASN1_UNDEF;
    X509_SIG *p8;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_epki_der_priv_bio called\n");

    if (!ctx->cipher_intent)
        return 0;

    if (p2s != NULL && !p2s(key, key_nid, ctx->save_parameters, &str, &strtype))
        return 0;

    p8 = key_to_encp8(key, key_nid, str, strtype, k2d, ctx);
    if (p8 != NULL)
        ret = i2d_PKCS8_bio(out, p8);

    X509_SIG_free(p8);

    return ret;
}

static int key_to_epki_pem_priv_bio(BIO *out, const void *key, int key_nid,
                                    ossl_unused const char *pemname,
                                    key_to_paramstring_fn *p2s,
                                    i2d_of_void *k2d,
                                    struct key2any_ctx_st *ctx) {
    int ret = 0;
    void *str = NULL;
    int strtype = V_ASN1_UNDEF;
    X509_SIG *p8;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_epki_pem_priv_bio called\n");

    if (!ctx->cipher_intent)
        return 0;

    if (p2s != NULL && !p2s(key, key_nid, ctx->save_parameters, &str, &strtype))
        return 0;

    p8 = key_to_encp8(key, key_nid, str, strtype, k2d, ctx);
    if (p8 != NULL)
        ret = PEM_write_bio_PKCS8(out, p8);

    X509_SIG_free(p8);

    return ret;
}

static int key_to_pki_der_priv_bio(BIO *out, const void *key, int key_nid,
                                   ossl_unused const char *pemname,
                                   key_to_paramstring_fn *p2s, i2d_of_void *k2d,
                                   struct key2any_ctx_st *ctx) {
    int ret = 0;
    void *str = NULL;
    int strtype = V_ASN1_UNDEF;
    PKCS8_PRIV_KEY_INFO *p8info;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_pki_der_priv_bio called\n");

    if (ctx->cipher_intent)
        return key_to_epki_der_priv_bio(out, key, key_nid, pemname, p2s, k2d,
                                        ctx);

    if (p2s != NULL && !p2s(key, key_nid, ctx->save_parameters, &str, &strtype))
        return 0;

    p8info = key_to_p8info(key, key_nid, str, strtype, k2d);

    if (p8info != NULL)
        ret = i2d_PKCS8_PRIV_KEY_INFO_bio(out, p8info);
    else
        free_asn1_data(strtype, str);

    PKCS8_PRIV_KEY_INFO_free(p8info);

    return ret;
}

static int key_to_pki_pem_priv_bio(BIO *out, const void *key, int key_nid,
                                   ossl_unused const char *pemname,
                                   key_to_paramstring_fn *p2s, i2d_of_void *k2d,
                                   struct key2any_ctx_st *ctx) {
    int ret = 0;
    void *str = NULL;
    int strtype = V_ASN1_UNDEF;
    PKCS8_PRIV_KEY_INFO *p8info;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_pki_pem_priv_bio called\n");

    if (ctx->cipher_intent)
        return key_to_epki_pem_priv_bio(out, key, key_nid, pemname, p2s, k2d,
                                        ctx);

    if (p2s != NULL && !p2s(key, key_nid, ctx->save_parameters, &str, &strtype))
        return 0;

    p8info = key_to_p8info(key, key_nid, str, strtype, k2d);
    if (p8info != NULL)
        ret = PEM_write_bio_PKCS8_PRIV_KEY_INFO(out, p8info);
    else
        free_asn1_data(strtype, str);

    PKCS8_PRIV_KEY_INFO_free(p8info);

    return ret;
}

static int key_to_spki_der_pub_bio(BIO *out, const void *key, int key_nid,
                                   ossl_unused const char *pemname,
                                   key_to_paramstring_fn *p2s, i2d_of_void *k2d,
                                   struct key2any_ctx_st *ctx) {
    int ret = 0;
    OQSX_KEY *okey = (OQSX_KEY *)key;
    X509_PUBKEY *xpk = NULL;
    void *str = NULL;
    int strtype = V_ASN1_UNDEF;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_spki_der_pub_bio called\n");

    if (p2s != NULL && !p2s(key, key_nid, ctx->save_parameters, &str, &strtype))
        return 0;

    xpk = oqsx_key_to_pubkey(key, key_nid, str, strtype, k2d);

    if (xpk != NULL)
        ret = i2d_X509_PUBKEY_bio(out, xpk);

    X509_PUBKEY_free(xpk);
    return ret;
}

static int key_to_spki_pem_pub_bio(BIO *out, const void *key, int key_nid,
                                   ossl_unused const char *pemname,
                                   key_to_paramstring_fn *p2s, i2d_of_void *k2d,
                                   struct key2any_ctx_st *ctx) {
    int ret = 0;
    X509_PUBKEY *xpk = NULL;
    void *str = NULL;
    int strtype = V_ASN1_UNDEF;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_spki_pem_pub_bio called\n");

    if (p2s != NULL && !p2s(key, key_nid, ctx->save_parameters, &str, &strtype))
        return 0;

    xpk = oqsx_key_to_pubkey(key, key_nid, str, strtype, k2d);

    if (xpk != NULL)
        ret = PEM_write_bio_X509_PUBKEY(out, xpk);
    else
        free_asn1_data(strtype, str);

    /* Also frees |str| */
    X509_PUBKEY_free(xpk);
    return ret;
}

/*
 * key_to_type_specific_* produce encoded output with type specific key data,
 * no envelopment; the same kind of output as the type specific i2d_ and
 * PEM_write_ functions, which is often a simple SEQUENCE of INTEGER.
 *
 * OpenSSL tries to discourage production of new keys in this form, because
 * of the ambiguity when trying to recognise them, but can't deny that PKCS#1
 * et al still are live standards.
 *
 * Note that these functions completely ignore p2s, and rather rely entirely
 * on k2d to do the complete work.
 */
/*
static int key_to_type_specific_der_bio(BIO *out, const void *key,
                                        int key_nid,
                                        ossl_unused const char *pemname,
                                        key_to_paramstring_fn *p2s,
                                        i2d_of_void *k2d,
                                        struct key2any_ctx_st *ctx)
{
    unsigned char *der = NULL;
    int derlen;
    int ret;

    OQS_ENC_PRINTF("OQS ENC provider: key_to_type_specific_der_bio called\n");

    if ((derlen = k2d(key, &der)) <= 0) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    ret = BIO_write(out, der, derlen);
    OPENSSL_free(der);
    return ret > 0;
}
#define key_to_type_specific_der_priv_bio key_to_type_specific_der_bio
#define key_to_type_specific_der_pub_bio key_to_type_specific_der_bio
#define key_to_type_specific_der_param_bio key_to_type_specific_der_bio

static int key_to_type_specific_pem_bio_cb(BIO *out, const void *key,
                                           int key_nid, const char *pemname,
                                           key_to_paramstring_fn *p2s,
                                           i2d_of_void *k2d,
                                           struct key2any_ctx_st *ctx)
{
    OQS_ENC_PRINTF("OQS ENC provider: key_to_type_specific_pem_bio_cb called
\n");

    return PEM_ASN1_write_bio(k2d, pemname, out, key, ctx->cipher,
                              NULL, 0, ctx->pwcb, ctx->pwcbarg) > 0;
}

static int key_to_type_specific_pem_priv_bio(BIO *out, const void *key,
                                             int key_nid, const char *pemname,
                                             key_to_paramstring_fn *p2s,
                                             i2d_of_void *k2d,
                                             struct key2any_ctx_st *ctx)
{
    OQS_ENC_PRINTF("OQS ENC provider: key_to_type_specific_pem_priv_bio
called\n");

    return key_to_type_specific_pem_bio_cb(out, key, key_nid, pemname,
                                           p2s, k2d, ctx, ctx->pwcb,
ctx->pwcbarg);

}

static int key_to_type_specific_pem_pub_bio(BIO *out, const void *key,
                                            int key_nid, const char *pemname,
                                            key_to_paramstring_fn *p2s,
                                            i2d_of_void *k2d,
                                            struct key2any_ctx_st *ctx)
{
    OQS_ENC_PRINTF("OQS ENC provider: key_to_type_specific_pem_pub_bio
called\n");

    return key_to_type_specific_pem_bio_cb(out, key, key_nid, pemname,
                                           p2s, k2d, ctx, NULL, NULL);
}

#ifndef OPENSSL_NO_KEYPARAMS
static int key_to_type_specific_pem_param_bio(BIO *out, const void *key,
                                              int key_nid, const char *pemname,
                                              key_to_paramstring_fn *p2s,
                                              i2d_of_void *k2d,
                                              struct key2any_ctx_st *ctx)
{
    OQS_ENC_PRINTF("OQS ENC provider: key_to_type_specific_pem_param_bio
called\n");

    return key_to_type_specific_pem_bio_cb(out, key, key_nid, pemname,
                                           p2s, k2d, ctx, NULL, NULL);
}
#endif
*/
/* ---------------------------------------------------------------------- */

static int prepare_oqsx_params(const void *oqsxkey, int nid, int save,
                               void **pstr, int *pstrtype) {
    ASN1_OBJECT *params = NULL;
    OQSX_KEY *k = (OQSX_KEY *)oqsxkey;

    OQS_ENC_PRINTF3("OQS ENC provider: prepare_oqsx_params called with nid %d "
                    "(tlsname: %s)\n",
                    nid, k->tls_name);

    if (k->tls_name && OBJ_sn2nid(k->tls_name) != nid) {
        ERR_raise(ERR_LIB_USER, OQSPROV_R_INVALID_KEY);
        return 0;
    }

    if (nid != NID_undef) {
        params = OBJ_nid2obj(nid);
        if (params == NULL)
            return 0;
    } else {
        ERR_raise(ERR_LIB_USER, OQSPROV_R_MISSING_OID);
        return 0;
    }

    if (OBJ_length(params) == 0) {
        /* unexpected error */
        ERR_raise(ERR_LIB_USER, OQSPROV_R_MISSING_OID);
        ASN1_OBJECT_free(params);
        return 0;
    }
    *pstr = params;
    *pstrtype = V_ASN1_OBJECT;
    return 1;
}

static int oqsx_spki_pub_to_der(const void *vxkey, unsigned char **pder) {
    const OQSX_KEY *oqsxkey = vxkey;
    unsigned char *keyblob;
    int ret = 0;

    OQS_ENC_PRINTF("OQS ENC provider: oqsx_spki_pub_to_der called\n");

    if (oqsxkey == NULL || oqsxkey->pubkey == NULL) {
        ERR_raise(ERR_LIB_USER, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    keyblob = OPENSSL_memdup(oqsxkey->pubkey, oqsxkey->pubkeylen);
    if (keyblob == NULL) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    *pder = keyblob;
    return oqsxkey->pubkeylen;
}

static int oqsx_pki_priv_to_der(const void *vxkey, unsigned char **pder) {
    OQSX_KEY *oqsxkey = (OQSX_KEY *)vxkey;
    unsigned char *buf = NULL;
    uint32_t buflen = 0, privkeylen = 0;
    ASN1_OCTET_STRING oct;
    int keybloblen;

    OQS_ENC_PRINTF("OQS ENC provider: oqsx_pki_priv_to_der called\n");

    // Encoding private _and_ public key concatenated ... seems unlogical and
    // unnecessary, but is what oqs-openssl does, so we repeat it for
    // interop... also from a security perspective not really smart to copy key
    // material (side channel attacks, anyone?), but so be it for now (TBC).
    if (oqsxkey == NULL || oqsxkey->privkey == NULL
#ifndef NOPUBKEY_IN_PRIVKEY
        || oqsxkey->pubkey == NULL
#endif
    ) {
        ERR_raise(ERR_LIB_USER, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }

    // only concatenate private classic key (if any) and OQS private and public
    // key NOT saving public classic key component (if any)
    privkeylen = oqsxkey->privkeylen;
    if (oqsxkey->numkeys > 1) { // hybrid
        uint32_t actualprivkeylen = 0;
        size_t fixed_pq_privkeylen =
            oqsxkey->oqsx_provider_ctx.oqsx_qs_ctx.kem->length_secret_key;
        size_t space_for_classical_privkey =
            privkeylen - SIZE_OF_UINT32 - fixed_pq_privkeylen;
        DECODE_UINT32(actualprivkeylen, oqsxkey->privkey);
        if ((actualprivkeylen > oqsxkey->evp_info->length_private_key) ||
            (actualprivkeylen > space_for_classical_privkey)) {
            ERR_raise(ERR_LIB_USER, OQSPROV_R_INVALID_ENCODING);
            return 0;
        }
        privkeylen -=
            (oqsxkey->evp_info->length_private_key - actualprivkeylen);
    }
#ifdef NOPUBKEY_IN_PRIVKEY
    buflen = privkeylen;
    buf = OPENSSL_secure_malloc(buflen);
    if (buf == NULL) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    OQS_ENC_PRINTF2("OQS ENC provider: saving privkey of length %zu\n", buflen);
    memcpy(buf, oqsxkey->privkey, privkeylen);
#else
    buflen = privkeylen + oqsx_key_get_oqs_public_key_len(oqsxkey);
    buf = OPENSSL_secure_malloc(buflen);
    if (buf == NULL) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    OQS_ENC_PRINTF2("OQS ENC provider: saving priv+pubkey of length %d\n",
                    buflen);
    memcpy(buf, oqsxkey->privkey, privkeylen);
    if (oqsxkey->reverse_share) {
        memcpy(buf + privkeylen, oqsxkey->comp_pubkey[0],
               oqsx_key_get_oqs_public_key_len(oqsxkey));
    } else {
        memcpy(buf + privkeylen, oqsxkey->comp_pubkey[oqsxkey->numkeys - 1],
               oqsx_key_get_oqs_public_key_len(oqsxkey));
    }
#endif

    oct.data = buf;
    oct.length = buflen;
    // more logical:
    // oct.data = oqsxkey->privkey;
    // oct.length = oqsxkey->privkeylen;
    oct.flags = 0;

    keybloblen = i2d_ASN1_OCTET_STRING(&oct, pder);
    if (keybloblen < 0) {
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        keybloblen = 0; // signal error
    }
    OPENSSL_secure_clear_free(buf, buflen);
    return keybloblen;
}

#define oqsx_epki_priv_to_der oqsx_pki_priv_to_der

/*
 * OQSX only has PKCS#8 / SubjectPublicKeyInfo
 * representation, so we don't define
 * oqsx_type_specific_[priv,pub,params]_to_der.
 */

#define oqsx_check_key_type NULL

// OQS provider uses NIDs generated at load time as EVP_type identifiers
// so initially this must be 0 and set to a real value by OBJ_sn2nid later
///// OQS_TEMPLATE_FRAGMENT_ENCODER_DEFINES_START
#define frodo640aes_evp_type 0
#define frodo640aes_input_type "frodo640aes"
#define frodo640aes_pem_type "frodo640aes"

#define p256_frodo640aes_evp_type 0
#define p256_frodo640aes_input_type "p256_frodo640aes"
#define p256_frodo640aes_pem_type "p256_frodo640aes"
#define x25519_frodo640aes_evp_type 0
#define x25519_frodo640aes_input_type "x25519_frodo640aes"
#define x25519_frodo640aes_pem_type "x25519_frodo640aes"
#define frodo640shake_evp_type 0
#define frodo640shake_input_type "frodo640shake"
#define frodo640shake_pem_type "frodo640shake"

#define p256_frodo640shake_evp_type 0
#define p256_frodo640shake_input_type "p256_frodo640shake"
#define p256_frodo640shake_pem_type "p256_frodo640shake"
#define x25519_frodo640shake_evp_type 0
#define x25519_frodo640shake_input_type "x25519_frodo640shake"
#define x25519_frodo640shake_pem_type "x25519_frodo640shake"
#define frodo976aes_evp_type 0
#define frodo976aes_input_type "frodo976aes"
#define frodo976aes_pem_type "frodo976aes"

#define p384_frodo976aes_evp_type 0
#define p384_frodo976aes_input_type "p384_frodo976aes"
#define p384_frodo976aes_pem_type "p384_frodo976aes"
#define x448_frodo976aes_evp_type 0
#define x448_frodo976aes_input_type "x448_frodo976aes"
#define x448_frodo976aes_pem_type "x448_frodo976aes"
#define frodo976shake_evp_type 0
#define frodo976shake_input_type "frodo976shake"
#define frodo976shake_pem_type "frodo976shake"

#define p384_frodo976shake_evp_type 0
#define p384_frodo976shake_input_type "p384_frodo976shake"
#define p384_frodo976shake_pem_type "p384_frodo976shake"
#define x448_frodo976shake_evp_type 0
#define x448_frodo976shake_input_type "x448_frodo976shake"
#define x448_frodo976shake_pem_type "x448_frodo976shake"
#define frodo1344aes_evp_type 0
#define frodo1344aes_input_type "frodo1344aes"
#define frodo1344aes_pem_type "frodo1344aes"

#define p521_frodo1344aes_evp_type 0
#define p521_frodo1344aes_input_type "p521_frodo1344aes"
#define p521_frodo1344aes_pem_type "p521_frodo1344aes"
#define frodo1344shake_evp_type 0
#define frodo1344shake_input_type "frodo1344shake"
#define frodo1344shake_pem_type "frodo1344shake"

#define p521_frodo1344shake_evp_type 0
#define p521_frodo1344shake_input_type "p521_frodo1344shake"
#define p521_frodo1344shake_pem_type "p521_frodo1344shake"
#define mlkem512_evp_type 0
#define mlkem512_input_type "mlkem512"
#define mlkem512_pem_type "mlkem512"

#define p256_mlkem512_evp_type 0
#define p256_mlkem512_input_type "p256_mlkem512"
#define p256_mlkem512_pem_type "p256_mlkem512"
#define x25519_mlkem512_evp_type 0
#define x25519_mlkem512_input_type "x25519_mlkem512"
#define x25519_mlkem512_pem_type "x25519_mlkem512"
#define mlkem768_evp_type 0
#define mlkem768_input_type "mlkem768"
#define mlkem768_pem_type "mlkem768"

#define p384_mlkem768_evp_type 0
#define p384_mlkem768_input_type "p384_mlkem768"
#define p384_mlkem768_pem_type "p384_mlkem768"
#define x448_mlkem768_evp_type 0
#define x448_mlkem768_input_type "x448_mlkem768"
#define x448_mlkem768_pem_type "x448_mlkem768"
#define X25519MLKEM768_evp_type 0
#define X25519MLKEM768_input_type "X25519MLKEM768"
#define X25519MLKEM768_pem_type "X25519MLKEM768"
#define SecP256r1MLKEM768_evp_type 0
#define SecP256r1MLKEM768_input_type "SecP256r1MLKEM768"
#define SecP256r1MLKEM768_pem_type "SecP256r1MLKEM768"
#define mlkem1024_evp_type 0
#define mlkem1024_input_type "mlkem1024"
#define mlkem1024_pem_type "mlkem1024"

#define p521_mlkem1024_evp_type 0
#define p521_mlkem1024_input_type "p521_mlkem1024"
#define p521_mlkem1024_pem_type "p521_mlkem1024"
#define SecP384r1MLKEM1024_evp_type 0
#define SecP384r1MLKEM1024_input_type "SecP384r1MLKEM1024"
#define SecP384r1MLKEM1024_pem_type "SecP384r1MLKEM1024"
#define bikel1_evp_type 0
#define bikel1_input_type "bikel1"
#define bikel1_pem_type "bikel1"

#define p256_bikel1_evp_type 0
#define p256_bikel1_input_type "p256_bikel1"
#define p256_bikel1_pem_type "p256_bikel1"
#define x25519_bikel1_evp_type 0
#define x25519_bikel1_input_type "x25519_bikel1"
#define x25519_bikel1_pem_type "x25519_bikel1"
#define bikel3_evp_type 0
#define bikel3_input_type "bikel3"
#define bikel3_pem_type "bikel3"

#define p384_bikel3_evp_type 0
#define p384_bikel3_input_type "p384_bikel3"
#define p384_bikel3_pem_type "p384_bikel3"
#define x448_bikel3_evp_type 0
#define x448_bikel3_input_type "x448_bikel3"
#define x448_bikel3_pem_type "x448_bikel3"
#define bikel5_evp_type 0
#define bikel5_input_type "bikel5"
#define bikel5_pem_type "bikel5"

#define p521_bikel5_evp_type 0
#define p521_bikel5_input_type "p521_bikel5"
#define p521_bikel5_pem_type "p521_bikel5"

#define mldsa44_evp_type 0
#define mldsa44_input_type "mldsa44"
#define mldsa44_pem_type "mldsa44"
#define p256_mldsa44_evp_type 0
#define p256_mldsa44_input_type "p256_mldsa44"
#define p256_mldsa44_pem_type "p256_mldsa44"
#define rsa3072_mldsa44_evp_type 0
#define rsa3072_mldsa44_input_type "rsa3072_mldsa44"
#define rsa3072_mldsa44_pem_type "rsa3072_mldsa44"
#define mldsa65_evp_type 0
#define mldsa65_input_type "mldsa65"
#define mldsa65_pem_type "mldsa65"
#define p384_mldsa65_evp_type 0
#define p384_mldsa65_input_type "p384_mldsa65"
#define p384_mldsa65_pem_type "p384_mldsa65"
#define mldsa87_evp_type 0
#define mldsa87_input_type "mldsa87"
#define mldsa87_pem_type "mldsa87"
#define p521_mldsa87_evp_type 0
#define p521_mldsa87_input_type "p521_mldsa87"
#define p521_mldsa87_pem_type "p521_mldsa87"
#define falcon512_evp_type 0
#define falcon512_input_type "falcon512"
#define falcon512_pem_type "falcon512"
#define p256_falcon512_evp_type 0
#define p256_falcon512_input_type "p256_falcon512"
#define p256_falcon512_pem_type "p256_falcon512"
#define rsa3072_falcon512_evp_type 0
#define rsa3072_falcon512_input_type "rsa3072_falcon512"
#define rsa3072_falcon512_pem_type "rsa3072_falcon512"
#define falconpadded512_evp_type 0
#define falconpadded512_input_type "falconpadded512"
#define falconpadded512_pem_type "falconpadded512"
#define p256_falconpadded512_evp_type 0
#define p256_falconpadded512_input_type "p256_falconpadded512"
#define p256_falconpadded512_pem_type "p256_falconpadded512"
#define rsa3072_falconpadded512_evp_type 0
#define rsa3072_falconpadded512_input_type "rsa3072_falconpadded512"
#define rsa3072_falconpadded512_pem_type "rsa3072_falconpadded512"
#define falcon1024_evp_type 0
#define falcon1024_input_type "falcon1024"
#define falcon1024_pem_type "falcon1024"
#define p521_falcon1024_evp_type 0
#define p521_falcon1024_input_type "p521_falcon1024"
#define p521_falcon1024_pem_type "p521_falcon1024"
#define falconpadded1024_evp_type 0
#define falconpadded1024_input_type "falconpadded1024"
#define falconpadded1024_pem_type "falconpadded1024"
#define p521_falconpadded1024_evp_type 0
#define p521_falconpadded1024_input_type "p521_falconpadded1024"
#define p521_falconpadded1024_pem_type "p521_falconpadded1024"
#define sphincssha2128fsimple_evp_type 0
#define sphincssha2128fsimple_input_type "sphincssha2128fsimple"
#define sphincssha2128fsimple_pem_type "sphincssha2128fsimple"
#define p256_sphincssha2128fsimple_evp_type 0
#define p256_sphincssha2128fsimple_input_type "p256_sphincssha2128fsimple"
#define p256_sphincssha2128fsimple_pem_type "p256_sphincssha2128fsimple"
#define rsa3072_sphincssha2128fsimple_evp_type 0
#define rsa3072_sphincssha2128fsimple_input_type "rsa3072_sphincssha2128fsimple"
#define rsa3072_sphincssha2128fsimple_pem_type "rsa3072_sphincssha2128fsimple"
#define sphincssha2128ssimple_evp_type 0
#define sphincssha2128ssimple_input_type "sphincssha2128ssimple"
#define sphincssha2128ssimple_pem_type "sphincssha2128ssimple"
#define p256_sphincssha2128ssimple_evp_type 0
#define p256_sphincssha2128ssimple_input_type "p256_sphincssha2128ssimple"
#define p256_sphincssha2128ssimple_pem_type "p256_sphincssha2128ssimple"
#define rsa3072_sphincssha2128ssimple_evp_type 0
#define rsa3072_sphincssha2128ssimple_input_type "rsa3072_sphincssha2128ssimple"
#define rsa3072_sphincssha2128ssimple_pem_type "rsa3072_sphincssha2128ssimple"
#define sphincssha2192fsimple_evp_type 0
#define sphincssha2192fsimple_input_type "sphincssha2192fsimple"
#define sphincssha2192fsimple_pem_type "sphincssha2192fsimple"
#define p384_sphincssha2192fsimple_evp_type 0
#define p384_sphincssha2192fsimple_input_type "p384_sphincssha2192fsimple"
#define p384_sphincssha2192fsimple_pem_type "p384_sphincssha2192fsimple"
#define sphincsshake128fsimple_evp_type 0
#define sphincsshake128fsimple_input_type "sphincsshake128fsimple"
#define sphincsshake128fsimple_pem_type "sphincsshake128fsimple"
#define p256_sphincsshake128fsimple_evp_type 0
#define p256_sphincsshake128fsimple_input_type "p256_sphincsshake128fsimple"
#define p256_sphincsshake128fsimple_pem_type "p256_sphincsshake128fsimple"
#define rsa3072_sphincsshake128fsimple_evp_type 0
#define rsa3072_sphincsshake128fsimple_input_type                              \
    "rsa3072_sphincsshake128fsimple"
#define rsa3072_sphincsshake128fsimple_pem_type "rsa3072_sphincsshake128fsimple"
#define mayo1_evp_type 0
#define mayo1_input_type "mayo1"
#define mayo1_pem_type "mayo1"
#define p256_mayo1_evp_type 0
#define p256_mayo1_input_type "p256_mayo1"
#define p256_mayo1_pem_type "p256_mayo1"
#define mayo2_evp_type 0
#define mayo2_input_type "mayo2"
#define mayo2_pem_type "mayo2"
#define p256_mayo2_evp_type 0
#define p256_mayo2_input_type "p256_mayo2"
#define p256_mayo2_pem_type "p256_mayo2"
#define mayo3_evp_type 0
#define mayo3_input_type "mayo3"
#define mayo3_pem_type "mayo3"
#define p384_mayo3_evp_type 0
#define p384_mayo3_input_type "p384_mayo3"
#define p384_mayo3_pem_type "p384_mayo3"
#define mayo5_evp_type 0
#define mayo5_input_type "mayo5"
#define mayo5_pem_type "mayo5"
#define p521_mayo5_evp_type 0
#define p521_mayo5_input_type "p521_mayo5"
#define p521_mayo5_pem_type "p521_mayo5"
#define CROSSrsdp128balanced_evp_type 0
#define CROSSrsdp128balanced_input_type "CROSSrsdp128balanced"
#define CROSSrsdp128balanced_pem_type "CROSSrsdp128balanced"
#define OV_Is_pkc_evp_type 0
#define OV_Is_pkc_input_type "OV_Is_pkc"
#define OV_Is_pkc_pem_type "OV_Is_pkc"
#define p256_OV_Is_pkc_evp_type 0
#define p256_OV_Is_pkc_input_type "p256_OV_Is_pkc"
#define p256_OV_Is_pkc_pem_type "p256_OV_Is_pkc"
#define OV_Ip_pkc_evp_type 0
#define OV_Ip_pkc_input_type "OV_Ip_pkc"
#define OV_Ip_pkc_pem_type "OV_Ip_pkc"
#define p256_OV_Ip_pkc_evp_type 0
#define p256_OV_Ip_pkc_input_type "p256_OV_Ip_pkc"
#define p256_OV_Ip_pkc_pem_type "p256_OV_Ip_pkc"
#define OV_Is_pkc_skc_evp_type 0
#define OV_Is_pkc_skc_input_type "OV_Is_pkc_skc"
#define OV_Is_pkc_skc_pem_type "OV_Is_pkc_skc"
#define p256_OV_Is_pkc_skc_evp_type 0
#define p256_OV_Is_pkc_skc_input_type "p256_OV_Is_pkc_skc"
#define p256_OV_Is_pkc_skc_pem_type "p256_OV_Is_pkc_skc"
#define OV_Ip_pkc_skc_evp_type 0
#define OV_Ip_pkc_skc_input_type "OV_Ip_pkc_skc"
#define OV_Ip_pkc_skc_pem_type "OV_Ip_pkc_skc"
#define p256_OV_Ip_pkc_skc_evp_type 0
#define p256_OV_Ip_pkc_skc_input_type "p256_OV_Ip_pkc_skc"
#define p256_OV_Ip_pkc_skc_pem_type "p256_OV_Ip_pkc_skc"
#define snova2454_evp_type 0
#define snova2454_input_type "snova2454"
#define snova2454_pem_type "snova2454"
#define p256_snova2454_evp_type 0
#define p256_snova2454_input_type "p256_snova2454"
#define p256_snova2454_pem_type "p256_snova2454"
#define snova2454esk_evp_type 0
#define snova2454esk_input_type "snova2454esk"
#define snova2454esk_pem_type "snova2454esk"
#define p256_snova2454esk_evp_type 0
#define p256_snova2454esk_input_type "p256_snova2454esk"
#define p256_snova2454esk_pem_type "p256_snova2454esk"
#define snova37172_evp_type 0
#define snova37172_input_type "snova37172"
#define snova37172_pem_type "snova37172"
#define p256_snova37172_evp_type 0
#define p256_snova37172_input_type "p256_snova37172"
#define p256_snova37172_pem_type "p256_snova37172"
#define snova2455_evp_type 0
#define snova2455_input_type "snova2455"
#define snova2455_pem_type "snova2455"
#define p384_snova2455_evp_type 0
#define p384_snova2455_input_type "p384_snova2455"
#define p384_snova2455_pem_type "p384_snova2455"
#define snova2965_evp_type 0
#define snova2965_input_type "snova2965"
#define snova2965_pem_type "snova2965"
#define p521_snova2965_evp_type 0
#define p521_snova2965_input_type "p521_snova2965"
#define p521_snova2965_pem_type "p521_snova2965"
///// OQS_TEMPLATE_FRAGMENT_ENCODER_DEFINES_END

/* ---------------------------------------------------------------------- */

static OSSL_FUNC_decoder_newctx_fn key2any_newctx;
static OSSL_FUNC_decoder_freectx_fn key2any_freectx;

static void *key2any_newctx(void *provctx) {
    struct key2any_ctx_st *ctx = OPENSSL_zalloc(sizeof(*ctx));

    OQS_ENC_PRINTF("OQS ENC provider: key2any_newctx called\n");

    if (ctx != NULL) {
        ctx->provctx = provctx;
        ctx->save_parameters = 1;
    }

    return ctx;
}

static void key2any_freectx(void *vctx) {
    struct key2any_ctx_st *ctx = vctx;

    OQS_ENC_PRINTF("OQS ENC provider: key2any_freectx called\n");

    EVP_CIPHER_free(ctx->cipher);
    OPENSSL_free(ctx);
}

static const OSSL_PARAM *
key2any_settable_ctx_params(ossl_unused void *provctx) {
    static const OSSL_PARAM settables[] = {
        OSSL_PARAM_utf8_string(OSSL_ENCODER_PARAM_CIPHER, NULL, 0),
        OSSL_PARAM_utf8_string(OSSL_ENCODER_PARAM_PROPERTIES, NULL, 0),
        OSSL_PARAM_END,
    };

    OQS_ENC_PRINTF("OQS ENC provider: key2any_settable_ctx_params called\n");

    return settables;
}

static int key2any_set_ctx_params(void *vctx, const OSSL_PARAM params[]) {
    struct key2any_ctx_st *ctx = vctx;
    OSSL_LIB_CTX *libctx = ctx->provctx->libctx;
    const OSSL_PARAM *cipherp =
        OSSL_PARAM_locate_const(params, OSSL_ENCODER_PARAM_CIPHER);
    const OSSL_PARAM *propsp =
        OSSL_PARAM_locate_const(params, OSSL_ENCODER_PARAM_PROPERTIES);
    const OSSL_PARAM *save_paramsp =
        OSSL_PARAM_locate_const(params, OSSL_ENCODER_PARAM_SAVE_PARAMETERS);

    OQS_ENC_PRINTF("OQS ENC provider: key2any_set_ctx_params called\n");

    if (cipherp != NULL) {
        const char *ciphername = NULL;
        const char *props = NULL;

        if (!OSSL_PARAM_get_utf8_string_ptr(cipherp, &ciphername))
            return 0;
        OQS_ENC_PRINTF2(" setting cipher: %s\n", ciphername);
        if (propsp != NULL && !OSSL_PARAM_get_utf8_string_ptr(propsp, &props))
            return 0;

        EVP_CIPHER_free(ctx->cipher);
        ctx->cipher = NULL;
        ctx->cipher_intent = ciphername != NULL;
        if (ciphername != NULL && ((ctx->cipher = EVP_CIPHER_fetch(
                                        libctx, ciphername, props)) == NULL)) {
            return 0;
        }
    }

    if (save_paramsp != NULL) {
        if (!OSSL_PARAM_get_int(save_paramsp, &ctx->save_parameters)) {
            return 0;
        }
    }
    OQS_ENC_PRINTF2(" cipher set to %p: \n", ctx->cipher);
    // not passing in a cipher param will lead to no-op hence no error
    return 1;
}

static int key2any_check_selection(int selection, int selection_mask) {
    /*
     * The selections are kinda sorta "levels", i.e. each selection given
     * here is assumed to include those following.
     */
    int checks[] = {OSSL_KEYMGMT_SELECT_PRIVATE_KEY,
                    OSSL_KEYMGMT_SELECT_PUBLIC_KEY,
                    OSSL_KEYMGMT_SELECT_ALL_PARAMETERS};
    size_t i;

    OQS_ENC_PRINTF3("OQS ENC provider: key2any_check_selection called with "
                    "selection %d (%d)\n",
                    selection, selection_mask);

    /* The decoder implementations made here support guessing */
    if (selection == 0)
        return 1;

    for (i = 0; i < OSSL_NELEM(checks); i++) {
        int check1 = (selection & checks[i]) != 0;
        int check2 = (selection_mask & checks[i]) != 0;

        /*
         * If the caller asked for the currently checked bit(s), return
         * whether the decoder description says it's supported.
         */
        if (check1) {
            OQS_ENC_PRINTF2("OQS ENC provider: "
                            "key2any_check_selection returns %d\n",
                            check2);
            return check2;
        }
    }

    /* This should be dead code, but just to be safe... */
    return 0;
}

static int key2any_encode(struct key2any_ctx_st *ctx, OSSL_CORE_BIO *cout,
                          const void *key, const char *typestr,
                          const char *pemname, key_to_der_fn *writer,
                          OSSL_PASSPHRASE_CALLBACK *pwcb, void *pwcbarg,
                          key_to_paramstring_fn *key2paramstring,
                          i2d_of_void *key2der) {
    int ret = 0;
    int type = OBJ_sn2nid(typestr);
    OQSX_KEY *oqsk = (OQSX_KEY *)key;

    OQS_ENC_PRINTF3(
        "OQS ENC provider: key2any_encode called with type %d (%s)\n", type,
        typestr);
    OQS_ENC_PRINTF2("OQS ENC provider: key2any_encode called with pemname %s\n",
                    pemname);

    if (key == NULL || type <= 0) {
        ERR_raise(ERR_LIB_USER, ERR_R_PASSED_NULL_PARAMETER);
    } else if (writer != NULL) {
        // Is ref counting really needed? For now, do it as per
        // https://beta.openssl.org/docs/manmaster/man3/BIO_new_from_core_bio.html:
        BIO *out = oqs_bio_new_from_core_bio(ctx->provctx, cout);

        if (out != NULL) {
            ctx->pwcb = pwcb;
            ctx->pwcbarg = pwcbarg;

            ret =
                writer(out, key, type, pemname, key2paramstring, key2der, ctx);
        }

        BIO_free(out);
    } else {
        ERR_raise(ERR_LIB_USER, ERR_R_PASSED_INVALID_ARGUMENT);
    }
    OQS_ENC_PRINTF2(" encode result: %d\n", ret);
    return ret;
}

#define DO_PRIVATE_KEY_selection_mask OSSL_KEYMGMT_SELECT_PRIVATE_KEY
#define DO_PRIVATE_KEY(impl, type, kind, output)                               \
    if ((selection & DO_PRIVATE_KEY_selection_mask) != 0)                      \
        return key2any_encode(                                                 \
            ctx, cout, key, impl##_pem_type, impl##_pem_type " PRIVATE KEY",   \
            key_to_##kind##_##output##_priv_bio, cb, cbarg,                    \
            prepare_##type##_params, type##_##kind##_priv_to_der);

#define DO_PUBLIC_KEY_selection_mask OSSL_KEYMGMT_SELECT_PUBLIC_KEY
#define DO_PUBLIC_KEY(impl, type, kind, output)                                \
    if ((selection & DO_PUBLIC_KEY_selection_mask) != 0)                       \
        return key2any_encode(                                                 \
            ctx, cout, key, impl##_pem_type, impl##_pem_type " PUBLIC KEY",    \
            key_to_##kind##_##output##_pub_bio, cb, cbarg,                     \
            prepare_##type##_params, type##_##kind##_pub_to_der);

#define DO_PARAMETERS_selection_mask OSSL_KEYMGMT_SELECT_ALL_PARAMETERS
#define DO_PARAMETERS(impl, type, kind, output)                                \
    if ((selection & DO_PARAMETERS_selection_mask) != 0)                       \
        return key2any_encode(ctx, cout, key, impl##_pem_type,                 \
                              impl##_pem_type " PARAMETERS",                   \
                              key_to_##kind##_##output##_param_bio, NULL,      \
                              NULL, NULL, type##_##kind##_params_to_der);

/*-
 * Implement the kinds of output structure that can be produced.  They are
 * referred to by name, and for each name, the following macros are defined
 * (braces not included):
 *
 * DO_{kind}_selection_mask
 *
 *      A mask of selection bits that must not be zero.  This is used as a
 *      selection criterion for each implementation.
 *      This mask must never be zero.
 *
 * DO_{kind}
 *
 *      The performing macro.  It must use the DO_ macros defined above,
 *      always in this order:
 *
 *      - DO_PRIVATE_KEY
 *      - DO_PUBLIC_KEY
 *      - DO_PARAMETERS
 *
 *      Any of those may be omitted, but the relative order must still be
 *      the same.
 */

/*
 * PKCS#8 defines two structures for private keys only:
 * - PrivateKeyInfo             (raw unencrypted form)
 * - EncryptedPrivateKeyInfo    (encrypted wrapping)
 *
 * To allow a certain amount of flexibility, we allow the routines
 * for PrivateKeyInfo to also produce EncryptedPrivateKeyInfo if a
 * passphrase callback has been passed to them.
 */
#define DO_PrivateKeyInfo_selection_mask DO_PRIVATE_KEY_selection_mask
#define DO_PrivateKeyInfo(impl, type, output)                                  \
    DO_PRIVATE_KEY(impl, type, pki, output)

#define DO_EncryptedPrivateKeyInfo_selection_mask DO_PRIVATE_KEY_selection_mask
#define DO_EncryptedPrivateKeyInfo(impl, type, output)                         \
    DO_PRIVATE_KEY(impl, type, epki, output)

/* SubjectPublicKeyInfo is a structure for public keys only */
#define DO_SubjectPublicKeyInfo_selection_mask DO_PUBLIC_KEY_selection_mask
#define DO_SubjectPublicKeyInfo(impl, type, output)                            \
    DO_PUBLIC_KEY(impl, type, spki, output)

/*
 * "type-specific" is a uniform name for key type specific output for private
 * and public keys as well as key parameters.  This is used internally in
 * libcrypto so it doesn't have to have special knowledge about select key
 * types, but also when no better name has been found.  If there are more
 * expressive DO_ names above, those are preferred.
 *
 * Three forms exist:
 *
 * - type_specific_keypair              Only supports private and public key
 * - type_specific_params               Only supports parameters
 * - type_specific                      Supports all parts of an EVP_PKEY
 * - type_specific_no_pub               Supports all parts of an EVP_PKEY
 *                                      except public key
 */
#define DO_type_specific_params_selection_mask DO_PARAMETERS_selection_mask
#define DO_type_specific_params(impl, type, output)                            \
    DO_PARAMETERS(impl, type, type_specific, output)
#define DO_type_specific_keypair_selection_mask                                \
    (DO_PRIVATE_KEY_selection_mask | DO_PUBLIC_KEY_selection_mask)
#define DO_type_specific_keypair(impl, type, output)                           \
    DO_PRIVATE_KEY(impl, type, type_specific, output)                          \
    DO_PUBLIC_KEY(impl, type, type_specific, output)
#define DO_type_specific_selection_mask                                        \
    (DO_type_specific_keypair_selection_mask |                                 \
     DO_type_specific_params_selection_mask)
#define DO_type_specific(impl, type, output)                                   \
    DO_type_specific_keypair(impl, type, output)                               \
        DO_type_specific_params(impl, type, output)
#define DO_type_specific_no_pub_selection_mask                                 \
    (DO_PRIVATE_KEY_selection_mask | DO_PARAMETERS_selection_mask)
#define DO_type_specific_no_pub(impl, type, output)                            \
    DO_PRIVATE_KEY(impl, type, type_specific, output)                          \
    DO_type_specific_params(impl, type, output)

/*
 * MAKE_ENCODER is the single driver for creating OSSL_DISPATCH tables.
 * It takes the following arguments:
 *
 * oqskemhyb    OQS KEM hybrid prefix; possibly empty
 * impl         This is the key type name that's being implemented.
 * type         This is the type name for the set of functions that implement
 *              the key type.  For example, ed25519, ed448, x25519 and x448
 *              are all implemented with the exact same set of functions.
 * kind         What kind of support to implement.  These translate into
 *              the DO_##kind macros above.
 * output       The output type to implement.  may be der or pem.
 *
 * The resulting OSSL_DISPATCH array gets the following name (expressed in
 * C preprocessor terms) from those arguments:
 *
 * oqs_##impl##_to_##kind##_##output##_encoder_functions
 */
#define MAKE_ENCODER(oqskemhyb, impl, type, kind, output)                      \
    static OSSL_FUNC_encoder_import_object_fn                                  \
        impl##_to_##kind##_##output##_import_object;                           \
    static OSSL_FUNC_encoder_free_object_fn                                    \
        impl##_to_##kind##_##output##_free_object;                             \
    static OSSL_FUNC_encoder_encode_fn impl##_to_##kind##_##output##_encode;   \
                                                                               \
    static void *impl##_to_##kind##_##output##_import_object(                  \
        void *vctx, int selection, const OSSL_PARAM params[]) {                \
        struct key2any_ctx_st *ctx = vctx;                                     \
                                                                               \
        OQS_ENC_PRINTF("OQS ENC provider: _import_object called\n");           \
        return oqs_prov_import_key(                                            \
            oqs##oqskemhyb##_##impl##_keymgmt_functions, ctx->provctx,         \
            selection, params);                                                \
    }                                                                          \
    static void impl##_to_##kind##_##output##_free_object(void *key) {         \
        OQS_ENC_PRINTF("OQS ENC provider: _free_object called\n");             \
        oqs_prov_free_key(oqs##oqskemhyb##_##impl##_keymgmt_functions, key);   \
    }                                                                          \
    static int impl##_to_##kind##_##output##_does_selection(void *ctx,         \
                                                            int selection) {   \
        OQS_ENC_PRINTF("OQS ENC provider: _does_selection called\n");          \
        return key2any_check_selection(selection, DO_##kind##_selection_mask); \
    }                                                                          \
    static int impl##_to_##kind##_##output##_encode(                           \
        void *ctx, OSSL_CORE_BIO *cout, const void *key,                       \
        const OSSL_PARAM key_abstract[], int selection,                        \
        OSSL_PASSPHRASE_CALLBACK *cb, void *cbarg) {                           \
        /* We don't deal with abstract objects */                              \
        OQS_ENC_PRINTF("OQS ENC provider: _encode called\n");                  \
        if (key_abstract != NULL) {                                            \
            ERR_raise(ERR_LIB_USER, ERR_R_PASSED_INVALID_ARGUMENT);            \
            return 0;                                                          \
        }                                                                      \
        DO_##kind(impl, type, output)                                          \
                                                                               \
            ERR_raise(ERR_LIB_USER, ERR_R_PASSED_INVALID_ARGUMENT);            \
        return 0;                                                              \
    }                                                                          \
    const OSSL_DISPATCH                                                        \
        oqs_##impl##_to_##kind##_##output##_encoder_functions[] = {            \
            {OSSL_FUNC_ENCODER_NEWCTX, (void (*)(void))key2any_newctx},        \
            {OSSL_FUNC_ENCODER_FREECTX, (void (*)(void))key2any_freectx},      \
            {OSSL_FUNC_ENCODER_SETTABLE_CTX_PARAMS,                            \
             (void (*)(void))key2any_settable_ctx_params},                     \
            {OSSL_FUNC_ENCODER_SET_CTX_PARAMS,                                 \
             (void (*)(void))key2any_set_ctx_params},                          \
            {OSSL_FUNC_ENCODER_DOES_SELECTION,                                 \
             (void (*)(void))impl##_to_##kind##_##output##_does_selection},    \
            {OSSL_FUNC_ENCODER_IMPORT_OBJECT,                                  \
             (void (*)(void))impl##_to_##kind##_##output##_import_object},     \
            {OSSL_FUNC_ENCODER_FREE_OBJECT,                                    \
             (void (*)(void))impl##_to_##kind##_##output##_free_object},       \
            {OSSL_FUNC_ENCODER_ENCODE,                                         \
             (void (*)(void))impl##_to_##kind##_##output##_encode},            \
            {0, NULL}}

/* ---------------------------------------------------------------------- */

/* steal from openssl/providers/implementations/encode_decode/encode_key2text.c
 */

#define LABELED_BUF_PRINT_WIDTH 15

static int print_labeled_buf(BIO *out, const char *label,
                             const unsigned char *buf, size_t buflen) {
    size_t i;

    if (BIO_printf(out, "%s\n", label) <= 0)
        return 0;

    for (i = 0; i < buflen; i++) {
        if ((i % LABELED_BUF_PRINT_WIDTH) == 0) {
            if (i > 0 && BIO_printf(out, "\n") <= 0)
                return 0;
            if (BIO_printf(out, "    ") <= 0)
                return 0;
        }

        if (BIO_printf(out, "%02x%s", buf[i], (i == buflen - 1) ? "" : ":") <=
            0)
            return 0;
    }
    if (BIO_printf(out, "\n") <= 0)
        return 0;

    return 1;
}

static int oqsx_to_text(BIO *out, const void *key, int selection) {
    OQSX_KEY *okey = (OQSX_KEY *)key;

    if (out == NULL || okey == NULL) {
        ERR_raise(ERR_LIB_USER, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }

    if ((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0) {
        if (okey->privkey == NULL) {
            ERR_raise(ERR_LIB_USER, PROV_R_NOT_A_PRIVATE_KEY);
            return 0;
        }

        switch (okey->keytype) {
        case KEY_TYPE_SIG:
        case KEY_TYPE_KEM:
            if (BIO_printf(out, "%s private key:\n", okey->tls_name) <= 0)
                return 0;
            break;
        case KEY_TYPE_ECP_HYB_KEM:
        case KEY_TYPE_ECX_HYB_KEM:
        case KEY_TYPE_HYB_SIG:
            if (BIO_printf(out, "%s hybrid private key:\n", okey->tls_name) <=
                0)
                return 0;
            break;
        default:
            ERR_raise(ERR_LIB_USER, OQSPROV_R_INVALID_KEY);
            return 0;
        }
    } else if ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0) {
        if (okey->pubkey == NULL) {
            ERR_raise(ERR_LIB_USER, PROV_R_NOT_A_PUBLIC_KEY);
            return 0;
        }

        switch (okey->keytype) {
        case KEY_TYPE_SIG:
        case KEY_TYPE_KEM:
            if (BIO_printf(out, "%s public key:\n", okey->tls_name) <= 0)
                return 0;
            break;
        case KEY_TYPE_ECP_HYB_KEM:
        case KEY_TYPE_ECX_HYB_KEM:
        case KEY_TYPE_HYB_SIG:
            if (BIO_printf(out, "%s hybrid public key:\n", okey->tls_name) <= 0)
                return 0;
            break;
        default:
            ERR_raise(ERR_LIB_USER, OQSPROV_R_INVALID_KEY);
            return 0;
        }
    }

    if ((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0) {
        if (okey->privkey) {
            if (okey->numkeys > 1) { // hybrid key
                char classic_label[200];
                uint32_t classic_key_len = 0;
                size_t fixed_pq_privkey_len =
                    okey->oqsx_provider_ctx.oqsx_qs_ctx.kem->length_secret_key;
                size_t space_for_classical_privkey =
                    okey->privkeylen - SIZE_OF_UINT32 - fixed_pq_privkey_len;
                sprintf(classic_label,
                        "%s key material:", OBJ_nid2sn(okey->evp_info->nid));
                DECODE_UINT32(classic_key_len, okey->privkey);
                if (classic_key_len > space_for_classical_privkey) {
                    ERR_raise(ERR_LIB_USER, OQSPROV_R_INVALID_ENCODING);
                    return 0;
                }
                if (!print_labeled_buf(out, classic_label,
                                       okey->comp_privkey[0], classic_key_len))
                    return 0;
                /* finally print pure PQ key */
                if (!print_labeled_buf(out, "PQ key material:",
                                       okey->comp_privkey[okey->numkeys - 1],
                                       okey->privkeylen - classic_key_len -
                                           SIZE_OF_UINT32))
                    return 0;
            } else { // plain PQ key
                if (!print_labeled_buf(out, "PQ key material:",
                                       okey->comp_privkey[okey->numkeys - 1],
                                       okey->privkeylen))
                    return 0;
            }
        }
    }
    if ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0) {
        if (okey->pubkey) {
            if (okey->numkeys > 1) { // hybrid key
                char classic_label[200];
                uint32_t classic_key_len = 0;
                size_t fixed_pq_pubkey_len =
                    okey->oqsx_provider_ctx.oqsx_qs_ctx.kem->length_public_key;
                size_t space_for_classical_pubkey =
                    okey->pubkeylen - SIZE_OF_UINT32 - fixed_pq_pubkey_len;
                DECODE_UINT32(classic_key_len, okey->pubkey);
                if (classic_key_len > space_for_classical_pubkey) {
                    ERR_raise(ERR_LIB_USER, OQSPROV_R_INVALID_ENCODING);
                    return 0;
                }
                sprintf(classic_label,
                        "%s key material:", OBJ_nid2sn(okey->evp_info->nid));
                if (!print_labeled_buf(out, classic_label, okey->comp_pubkey[0],
                                       classic_key_len))
                    return 0;
                /* finally print pure PQ key */
                if (!print_labeled_buf(out, "PQ key material:",
                                       okey->comp_pubkey[okey->numkeys - 1],
                                       okey->pubkeylen - classic_key_len -
                                           SIZE_OF_UINT32))
                    return 0;
            } else { // PQ key only
                if (!print_labeled_buf(out, "PQ key material:",
                                       okey->comp_pubkey[okey->numkeys - 1],
                                       okey->pubkeylen))
                    return 0;
            }
        }
    }

    return 1;
}

static void *key2text_newctx(void *provctx) { return provctx; }

static void key2text_freectx(ossl_unused void *vctx) {}

static int
key2text_encode(void *vctx, const void *key, int selection, OSSL_CORE_BIO *cout,
                int (*key2text)(BIO *out, const void *key, int selection),
                OSSL_PASSPHRASE_CALLBACK *cb, void *cbarg) {
    BIO *out = oqs_bio_new_from_core_bio(vctx, cout);
    int ret;

    if (out == NULL)
        return 0;

    ret = key2text(out, key, selection);
    BIO_free(out);

    return ret;
}

#define MAKE_TEXT_ENCODER(oqskemhyb, impl)                                     \
    static OSSL_FUNC_encoder_import_object_fn impl##2text_import_object;       \
    static OSSL_FUNC_encoder_free_object_fn impl##2text_free_object;           \
    static OSSL_FUNC_encoder_encode_fn impl##2text_encode;                     \
                                                                               \
    static void *impl##2text_import_object(void *ctx, int selection,           \
                                           const OSSL_PARAM params[]) {        \
        return oqs_prov_import_key(                                            \
            oqs##oqskemhyb##_##impl##_keymgmt_functions, ctx, selection,       \
            params);                                                           \
    }                                                                          \
    static void impl##2text_free_object(void *key) {                           \
        oqs_prov_free_key(oqs##oqskemhyb##_##impl##_keymgmt_functions, key);   \
    }                                                                          \
    static int impl##2text_encode(                                             \
        void *vctx, OSSL_CORE_BIO *cout, const void *key,                      \
        const OSSL_PARAM key_abstract[], int selection,                        \
        OSSL_PASSPHRASE_CALLBACK *cb, void *cbarg) {                           \
        /* We don't deal with abstract objects */                              \
        if (key_abstract != NULL) {                                            \
            ERR_raise(ERR_LIB_USER, ERR_R_PASSED_INVALID_ARGUMENT);            \
            return 0;                                                          \
        }                                                                      \
        return key2text_encode(vctx, key, selection, cout, oqsx_to_text, cb,   \
                               cbarg);                                         \
    }                                                                          \
    const OSSL_DISPATCH oqs_##impl##_to_text_encoder_functions[] = {           \
        {OSSL_FUNC_ENCODER_NEWCTX, (void (*)(void))key2text_newctx},           \
        {OSSL_FUNC_ENCODER_FREECTX, (void (*)(void))key2text_freectx},         \
        {OSSL_FUNC_ENCODER_IMPORT_OBJECT,                                      \
         (void (*)(void))impl##2text_import_object},                           \
        {OSSL_FUNC_ENCODER_FREE_OBJECT,                                        \
         (void (*)(void))impl##2text_free_object},                             \
        {OSSL_FUNC_ENCODER_ENCODE, (void (*)(void))impl##2text_encode},        \
        {0, NULL}}

/*
 * Replacements for i2d_{TYPE}PrivateKey, i2d_{TYPE}PublicKey,
 * i2d_{TYPE}params, as they exist.
 */

/*
 * PKCS#8 and SubjectPublicKeyInfo support.  This may duplicate some of the
 * implementations specified above, but are more specific.
 * The SubjectPublicKeyInfo implementations also replace the
 * PEM_write_bio_{TYPE}_PUBKEY functions.
 * For PEM, these are expected to be used by PEM_write_bio_PrivateKey(),
 * PEM_write_bio_PUBKEY() and PEM_write_bio_Parameters().
 */
///// OQS_TEMPLATE_FRAGMENT_ENCODER_MAKE_START
#ifdef OQS_KEM_ENCODERS

MAKE_ENCODER(, frodo640aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, frodo640aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, frodo640aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, frodo640aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, frodo640aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, frodo640aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, frodo640aes);

MAKE_ENCODER(_ecp, p256_frodo640aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_frodo640aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_frodo640aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_frodo640aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_frodo640aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p256_frodo640aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p256_frodo640aes);
MAKE_ENCODER(_ecx, x25519_frodo640aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_frodo640aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_frodo640aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_frodo640aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_frodo640aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_frodo640aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x25519_frodo640aes);
MAKE_ENCODER(, frodo640shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, frodo640shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, frodo640shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, frodo640shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, frodo640shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, frodo640shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, frodo640shake);

MAKE_ENCODER(_ecp, p256_frodo640shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_frodo640shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_frodo640shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_frodo640shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_frodo640shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p256_frodo640shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p256_frodo640shake);
MAKE_ENCODER(_ecx, x25519_frodo640shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_frodo640shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_frodo640shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_frodo640shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_frodo640shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_frodo640shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x25519_frodo640shake);
MAKE_ENCODER(, frodo976aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, frodo976aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, frodo976aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, frodo976aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, frodo976aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, frodo976aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, frodo976aes);

MAKE_ENCODER(_ecp, p384_frodo976aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_frodo976aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_frodo976aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_frodo976aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_frodo976aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p384_frodo976aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p384_frodo976aes);
MAKE_ENCODER(_ecx, x448_frodo976aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_frodo976aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_frodo976aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_frodo976aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_frodo976aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x448_frodo976aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x448_frodo976aes);
MAKE_ENCODER(, frodo976shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, frodo976shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, frodo976shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, frodo976shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, frodo976shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, frodo976shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, frodo976shake);

MAKE_ENCODER(_ecp, p384_frodo976shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_frodo976shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_frodo976shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_frodo976shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_frodo976shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p384_frodo976shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p384_frodo976shake);
MAKE_ENCODER(_ecx, x448_frodo976shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_frodo976shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_frodo976shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_frodo976shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_frodo976shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x448_frodo976shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x448_frodo976shake);
MAKE_ENCODER(, frodo1344aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, frodo1344aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, frodo1344aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, frodo1344aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, frodo1344aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, frodo1344aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, frodo1344aes);

MAKE_ENCODER(_ecp, p521_frodo1344aes, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_frodo1344aes, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_frodo1344aes, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_frodo1344aes, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_frodo1344aes, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p521_frodo1344aes, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p521_frodo1344aes);
MAKE_ENCODER(, frodo1344shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, frodo1344shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, frodo1344shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, frodo1344shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, frodo1344shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, frodo1344shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, frodo1344shake);

MAKE_ENCODER(_ecp, p521_frodo1344shake, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_frodo1344shake, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_frodo1344shake, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_frodo1344shake, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_frodo1344shake, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p521_frodo1344shake, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p521_frodo1344shake);
MAKE_ENCODER(, mlkem512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mlkem512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mlkem512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mlkem512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mlkem512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mlkem512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mlkem512);

MAKE_ENCODER(_ecp, p256_mlkem512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_mlkem512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_mlkem512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_mlkem512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_mlkem512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p256_mlkem512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p256_mlkem512);
MAKE_ENCODER(_ecx, x25519_mlkem512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_mlkem512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_mlkem512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_mlkem512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_mlkem512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_mlkem512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x25519_mlkem512);
MAKE_ENCODER(, mlkem768, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mlkem768, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mlkem768, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mlkem768, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mlkem768, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mlkem768, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mlkem768);

MAKE_ENCODER(_ecp, p384_mlkem768, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_mlkem768, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_mlkem768, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_mlkem768, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_mlkem768, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p384_mlkem768, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p384_mlkem768);
MAKE_ENCODER(_ecx, x448_mlkem768, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_mlkem768, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_mlkem768, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_mlkem768, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_mlkem768, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x448_mlkem768, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x448_mlkem768);
MAKE_ENCODER(_ecx, X25519MLKEM768, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, X25519MLKEM768, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, X25519MLKEM768, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, X25519MLKEM768, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, X25519MLKEM768, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, X25519MLKEM768, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, X25519MLKEM768);
MAKE_ENCODER(_ecp, SecP256r1MLKEM768, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, SecP256r1MLKEM768, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, SecP256r1MLKEM768, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, SecP256r1MLKEM768, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, SecP256r1MLKEM768, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, SecP256r1MLKEM768, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, SecP256r1MLKEM768);
MAKE_ENCODER(, mlkem1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mlkem1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mlkem1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mlkem1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mlkem1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mlkem1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mlkem1024);

MAKE_ENCODER(_ecp, p521_mlkem1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_mlkem1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_mlkem1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_mlkem1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_mlkem1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p521_mlkem1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p521_mlkem1024);
MAKE_ENCODER(_ecp, SecP384r1MLKEM1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, SecP384r1MLKEM1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, SecP384r1MLKEM1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, SecP384r1MLKEM1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, SecP384r1MLKEM1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, SecP384r1MLKEM1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, SecP384r1MLKEM1024);
MAKE_ENCODER(, bikel1, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, bikel1, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, bikel1, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, bikel1, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, bikel1, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, bikel1, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, bikel1);

MAKE_ENCODER(_ecp, p256_bikel1, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_bikel1, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_bikel1, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p256_bikel1, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p256_bikel1, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p256_bikel1, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p256_bikel1);
MAKE_ENCODER(_ecx, x25519_bikel1, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_bikel1, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_bikel1, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_bikel1, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x25519_bikel1, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x25519_bikel1, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x25519_bikel1);
MAKE_ENCODER(, bikel3, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, bikel3, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, bikel3, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, bikel3, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, bikel3, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, bikel3, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, bikel3);

MAKE_ENCODER(_ecp, p384_bikel3, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_bikel3, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_bikel3, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p384_bikel3, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p384_bikel3, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p384_bikel3, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p384_bikel3);
MAKE_ENCODER(_ecx, x448_bikel3, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_bikel3, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_bikel3, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecx, x448_bikel3, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecx, x448_bikel3, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecx, x448_bikel3, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecx, x448_bikel3);
MAKE_ENCODER(, bikel5, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, bikel5, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, bikel5, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, bikel5, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, bikel5, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, bikel5, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, bikel5);

MAKE_ENCODER(_ecp, p521_bikel5, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_bikel5, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_bikel5, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(_ecp, p521_bikel5, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(_ecp, p521_bikel5, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(_ecp, p521_bikel5, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(_ecp, p521_bikel5);
#endif /* OQS_KEM_ENCODERS */

MAKE_ENCODER(, mldsa44, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mldsa44, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mldsa44, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mldsa44, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mldsa44, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mldsa44, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mldsa44);
MAKE_ENCODER(, p256_mldsa44, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_mldsa44, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_mldsa44, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_mldsa44, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_mldsa44, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_mldsa44, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_mldsa44);
MAKE_ENCODER(, rsa3072_mldsa44, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_mldsa44, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_mldsa44, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_mldsa44, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_mldsa44, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, rsa3072_mldsa44, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, rsa3072_mldsa44);
MAKE_ENCODER(, mldsa65, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mldsa65, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mldsa65, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mldsa65, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mldsa65, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mldsa65, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mldsa65);
MAKE_ENCODER(, p384_mldsa65, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p384_mldsa65, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p384_mldsa65, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p384_mldsa65, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p384_mldsa65, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p384_mldsa65, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p384_mldsa65);
MAKE_ENCODER(, mldsa87, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mldsa87, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mldsa87, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mldsa87, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mldsa87, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mldsa87, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mldsa87);
MAKE_ENCODER(, p521_mldsa87, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p521_mldsa87, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p521_mldsa87, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p521_mldsa87, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p521_mldsa87, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p521_mldsa87, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p521_mldsa87);
MAKE_ENCODER(, falcon512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, falcon512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, falcon512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, falcon512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, falcon512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, falcon512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, falcon512);
MAKE_ENCODER(, p256_falcon512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_falcon512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_falcon512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_falcon512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_falcon512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_falcon512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_falcon512);
MAKE_ENCODER(, rsa3072_falcon512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_falcon512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_falcon512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_falcon512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_falcon512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, rsa3072_falcon512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, rsa3072_falcon512);
MAKE_ENCODER(, falconpadded512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, falconpadded512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, falconpadded512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, falconpadded512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, falconpadded512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, falconpadded512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, falconpadded512);
MAKE_ENCODER(, p256_falconpadded512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_falconpadded512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_falconpadded512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_falconpadded512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_falconpadded512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_falconpadded512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_falconpadded512);
MAKE_ENCODER(, rsa3072_falconpadded512, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_falconpadded512, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_falconpadded512, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_falconpadded512, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_falconpadded512, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, rsa3072_falconpadded512, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, rsa3072_falconpadded512);
MAKE_ENCODER(, falcon1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, falcon1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, falcon1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, falcon1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, falcon1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, falcon1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, falcon1024);
MAKE_ENCODER(, p521_falcon1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p521_falcon1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p521_falcon1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p521_falcon1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p521_falcon1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p521_falcon1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p521_falcon1024);
MAKE_ENCODER(, falconpadded1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, falconpadded1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, falconpadded1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, falconpadded1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, falconpadded1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, falconpadded1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, falconpadded1024);
MAKE_ENCODER(, p521_falconpadded1024, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p521_falconpadded1024, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p521_falconpadded1024, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p521_falconpadded1024, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p521_falconpadded1024, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p521_falconpadded1024, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p521_falconpadded1024);
MAKE_ENCODER(, sphincssha2128fsimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, sphincssha2128fsimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, sphincssha2128fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, sphincssha2128fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, sphincssha2128fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, sphincssha2128fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, sphincssha2128fsimple);
MAKE_ENCODER(, p256_sphincssha2128fsimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_sphincssha2128fsimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_sphincssha2128fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_sphincssha2128fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_sphincssha2128fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_sphincssha2128fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_sphincssha2128fsimple);
MAKE_ENCODER(, rsa3072_sphincssha2128fsimple, oqsx, EncryptedPrivateKeyInfo,
             der);
MAKE_ENCODER(, rsa3072_sphincssha2128fsimple, oqsx, EncryptedPrivateKeyInfo,
             pem);
MAKE_ENCODER(, rsa3072_sphincssha2128fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_sphincssha2128fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_sphincssha2128fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, rsa3072_sphincssha2128fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, rsa3072_sphincssha2128fsimple);
MAKE_ENCODER(, sphincssha2128ssimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, sphincssha2128ssimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, sphincssha2128ssimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, sphincssha2128ssimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, sphincssha2128ssimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, sphincssha2128ssimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, sphincssha2128ssimple);
MAKE_ENCODER(, p256_sphincssha2128ssimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_sphincssha2128ssimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_sphincssha2128ssimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_sphincssha2128ssimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_sphincssha2128ssimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_sphincssha2128ssimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_sphincssha2128ssimple);
MAKE_ENCODER(, rsa3072_sphincssha2128ssimple, oqsx, EncryptedPrivateKeyInfo,
             der);
MAKE_ENCODER(, rsa3072_sphincssha2128ssimple, oqsx, EncryptedPrivateKeyInfo,
             pem);
MAKE_ENCODER(, rsa3072_sphincssha2128ssimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_sphincssha2128ssimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_sphincssha2128ssimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, rsa3072_sphincssha2128ssimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, rsa3072_sphincssha2128ssimple);
MAKE_ENCODER(, sphincssha2192fsimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, sphincssha2192fsimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, sphincssha2192fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, sphincssha2192fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, sphincssha2192fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, sphincssha2192fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, sphincssha2192fsimple);
MAKE_ENCODER(, p384_sphincssha2192fsimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p384_sphincssha2192fsimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p384_sphincssha2192fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p384_sphincssha2192fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p384_sphincssha2192fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p384_sphincssha2192fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p384_sphincssha2192fsimple);
MAKE_ENCODER(, sphincsshake128fsimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, sphincsshake128fsimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, sphincsshake128fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, sphincsshake128fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, sphincsshake128fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, sphincsshake128fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, sphincsshake128fsimple);
MAKE_ENCODER(, p256_sphincsshake128fsimple, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_sphincsshake128fsimple, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_sphincsshake128fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_sphincsshake128fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_sphincsshake128fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_sphincsshake128fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_sphincsshake128fsimple);
MAKE_ENCODER(, rsa3072_sphincsshake128fsimple, oqsx, EncryptedPrivateKeyInfo,
             der);
MAKE_ENCODER(, rsa3072_sphincsshake128fsimple, oqsx, EncryptedPrivateKeyInfo,
             pem);
MAKE_ENCODER(, rsa3072_sphincsshake128fsimple, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, rsa3072_sphincsshake128fsimple, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, rsa3072_sphincsshake128fsimple, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, rsa3072_sphincsshake128fsimple, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, rsa3072_sphincsshake128fsimple);
MAKE_ENCODER(, mayo1, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mayo1, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mayo1, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mayo1, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mayo1, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mayo1, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mayo1);
MAKE_ENCODER(, p256_mayo1, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_mayo1, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_mayo1, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_mayo1, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_mayo1, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_mayo1, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_mayo1);
MAKE_ENCODER(, mayo2, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mayo2, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mayo2, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mayo2, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mayo2, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mayo2, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mayo2);
MAKE_ENCODER(, p256_mayo2, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_mayo2, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_mayo2, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_mayo2, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_mayo2, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_mayo2, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_mayo2);
MAKE_ENCODER(, mayo3, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mayo3, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mayo3, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mayo3, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mayo3, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mayo3, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mayo3);
MAKE_ENCODER(, p384_mayo3, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p384_mayo3, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p384_mayo3, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p384_mayo3, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p384_mayo3, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p384_mayo3, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p384_mayo3);
MAKE_ENCODER(, mayo5, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, mayo5, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, mayo5, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, mayo5, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, mayo5, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, mayo5, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, mayo5);
MAKE_ENCODER(, p521_mayo5, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p521_mayo5, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p521_mayo5, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p521_mayo5, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p521_mayo5, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p521_mayo5, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p521_mayo5);
MAKE_ENCODER(, CROSSrsdp128balanced, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, CROSSrsdp128balanced, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, CROSSrsdp128balanced, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, CROSSrsdp128balanced, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, CROSSrsdp128balanced, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, CROSSrsdp128balanced, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, CROSSrsdp128balanced);
MAKE_ENCODER(, OV_Is_pkc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, OV_Is_pkc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Is_pkc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, OV_Is_pkc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Is_pkc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, OV_Is_pkc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, OV_Is_pkc);
MAKE_ENCODER(, p256_OV_Is_pkc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Is_pkc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Is_pkc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Is_pkc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Is_pkc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_OV_Is_pkc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_OV_Is_pkc);
MAKE_ENCODER(, OV_Ip_pkc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, OV_Ip_pkc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Ip_pkc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, OV_Ip_pkc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Ip_pkc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, OV_Ip_pkc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, OV_Ip_pkc);
MAKE_ENCODER(, p256_OV_Ip_pkc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Ip_pkc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Ip_pkc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Ip_pkc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Ip_pkc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_OV_Ip_pkc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_OV_Ip_pkc);
MAKE_ENCODER(, OV_Is_pkc_skc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, OV_Is_pkc_skc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Is_pkc_skc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, OV_Is_pkc_skc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Is_pkc_skc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, OV_Is_pkc_skc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, OV_Is_pkc_skc);
MAKE_ENCODER(, p256_OV_Is_pkc_skc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Is_pkc_skc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Is_pkc_skc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Is_pkc_skc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Is_pkc_skc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_OV_Is_pkc_skc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_OV_Is_pkc_skc);
MAKE_ENCODER(, OV_Ip_pkc_skc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, OV_Ip_pkc_skc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Ip_pkc_skc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, OV_Ip_pkc_skc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, OV_Ip_pkc_skc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, OV_Ip_pkc_skc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, OV_Ip_pkc_skc);
MAKE_ENCODER(, p256_OV_Ip_pkc_skc, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Ip_pkc_skc, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Ip_pkc_skc, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_OV_Ip_pkc_skc, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_OV_Ip_pkc_skc, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_OV_Ip_pkc_skc, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_OV_Ip_pkc_skc);
MAKE_ENCODER(, snova2454, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, snova2454, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, snova2454, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, snova2454, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, snova2454, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, snova2454, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, snova2454);
MAKE_ENCODER(, p256_snova2454, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_snova2454, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_snova2454, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_snova2454, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_snova2454, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_snova2454, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_snova2454);
MAKE_ENCODER(, snova2454esk, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, snova2454esk, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, snova2454esk, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, snova2454esk, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, snova2454esk, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, snova2454esk, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, snova2454esk);
MAKE_ENCODER(, p256_snova2454esk, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_snova2454esk, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_snova2454esk, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_snova2454esk, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_snova2454esk, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_snova2454esk, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_snova2454esk);
MAKE_ENCODER(, snova37172, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, snova37172, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, snova37172, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, snova37172, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, snova37172, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, snova37172, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, snova37172);
MAKE_ENCODER(, p256_snova37172, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p256_snova37172, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p256_snova37172, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p256_snova37172, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p256_snova37172, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p256_snova37172, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p256_snova37172);
MAKE_ENCODER(, snova2455, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, snova2455, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, snova2455, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, snova2455, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, snova2455, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, snova2455, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, snova2455);
MAKE_ENCODER(, p384_snova2455, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p384_snova2455, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p384_snova2455, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p384_snova2455, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p384_snova2455, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p384_snova2455, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p384_snova2455);
MAKE_ENCODER(, snova2965, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, snova2965, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, snova2965, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, snova2965, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, snova2965, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, snova2965, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, snova2965);
MAKE_ENCODER(, p521_snova2965, oqsx, EncryptedPrivateKeyInfo, der);
MAKE_ENCODER(, p521_snova2965, oqsx, EncryptedPrivateKeyInfo, pem);
MAKE_ENCODER(, p521_snova2965, oqsx, PrivateKeyInfo, der);
MAKE_ENCODER(, p521_snova2965, oqsx, PrivateKeyInfo, pem);
MAKE_ENCODER(, p521_snova2965, oqsx, SubjectPublicKeyInfo, der);
MAKE_ENCODER(, p521_snova2965, oqsx, SubjectPublicKeyInfo, pem);
MAKE_TEXT_ENCODER(, p521_snova2965);
///// OQS_TEMPLATE_FRAGMENT_ENCODER_MAKE_END
