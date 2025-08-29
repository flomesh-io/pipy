// SPDX-License-Identifier: Apache-2.0 AND MIT

/*
 * OQS OpenSSL 3 provider
 *
 * Code strongly inspired by OpenSSL ecx key management.
 *
 * ToDo: More testing in non-KEM cases
 */

#include <assert.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <string.h>

#include "openssl/param_build.h"
#include "oqs_prov.h"

// stolen from openssl/crypto/param_build_set.c as
// ossl_param_build_set_octet_string not public API:

int oqsx_param_build_set_octet_string(OSSL_PARAM_BLD *bld, OSSL_PARAM *p,
                                      const char *key,
                                      const unsigned char *data,
                                      size_t data_len) {
    if (bld != NULL)
        return OSSL_PARAM_BLD_push_octet_string(bld, key, data, data_len);

    p = OSSL_PARAM_locate(p, key);
    if (p != NULL)
        return OSSL_PARAM_set_octet_string(p, data, data_len);
    return 1;
}

#ifdef NDEBUG
#define OQS_KM_PRINTF(a)
#define OQS_KM_PRINTF2(a, b)
#define OQS_KM_PRINTF3(a, b, c)
#else
#define OQS_KM_PRINTF(a)                                                       \
    if (getenv("OQSKM"))                                                       \
    printf(a)
#define OQS_KM_PRINTF2(a, b)                                                   \
    if (getenv("OQSKM"))                                                       \
    printf(a, b)
#define OQS_KM_PRINTF3(a, b, c)                                                \
    if (getenv("OQSKM"))                                                       \
    printf(a, b, c)
#endif // NDEBUG

// our own error codes:
#define OQSPROV_UNEXPECTED_NULL 1

static OSSL_FUNC_keymgmt_gen_cleanup_fn oqsx_gen_cleanup;
static OSSL_FUNC_keymgmt_load_fn oqsx_load;
static OSSL_FUNC_keymgmt_get_params_fn oqsx_get_params;
static OSSL_FUNC_keymgmt_gettable_params_fn oqs_gettable_params;
static OSSL_FUNC_keymgmt_set_params_fn oqsx_set_params;
static OSSL_FUNC_keymgmt_settable_params_fn oqsx_settable_params;
static OSSL_FUNC_keymgmt_has_fn oqsx_has;
static OSSL_FUNC_keymgmt_match_fn oqsx_match;
static OSSL_FUNC_keymgmt_import_fn oqsx_import;
static OSSL_FUNC_keymgmt_import_types_fn oqs_imexport_types;
static OSSL_FUNC_keymgmt_export_fn oqsx_export;
static OSSL_FUNC_keymgmt_export_types_fn oqs_imexport_types;

struct oqsx_gen_ctx {
    OSSL_LIB_CTX *libctx;
    char *propq;
    char *oqs_name;
    char *tls_name;
    int primitive;
    int selection;
    int bit_security;
    int alg_idx;
    int reverse_share;
};

static int oqsx_has(const void *keydata, int selection) {
    const OQSX_KEY *key = keydata;
    int ok = 0;

    OQS_KM_PRINTF("OQSKEYMGMT: has called\n");
    if (key != NULL) {
        /*
         * OQSX keys always have all the parameters they need (i.e. none).
         * Therefore we always return with 1, if asked about parameters.
         */
        ok = 1;

        if ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0)
            ok = ok && key->pubkey != NULL;

        if ((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0)
            ok = ok && key->privkey != NULL;
    }
    if (!ok)
        OQS_KM_PRINTF2("OQSKM: has returning FALSE on selection %2x\n",
                       selection);
    return ok;
}

/* Sets the index of the key components in a comp_privkey or comp_pubkey array
 */
static void oqsx_comp_set_idx(const OQSX_KEY *key, int *idx_classic,
                              int *idx_pq) {
    int reverse_share = (key->keytype == KEY_TYPE_ECP_HYB_KEM ||
                         key->keytype == KEY_TYPE_ECX_HYB_KEM) &&
                        key->reverse_share;

    if (reverse_share) {
        if (idx_classic)
            *idx_classic = key->numkeys - 1;
        if (idx_pq)
            *idx_pq = 0;
    } else {
        if (idx_classic)
            *idx_classic = 0;
        if (idx_pq)
            *idx_pq = key->numkeys - 1;
    }
}

/*
 * Key matching has a problem in OQS world: OpenSSL assumes all keys to (also)
 * contain public key material
 * (https://www.openssl.org/docs/manmaster/man3/EVP_PKEY_eq.html). This is not
 * the case with decoded private keys: Not all algorithms permit re-creating
 * public key material from private keys
 * (https://github.com/PQClean/PQClean/issues/415#issuecomment-910377682). Thus
 * we implement the following logic: 1) Private keys are matched binary if
 * available in both keys; only one key having private key material will be
 * considered a mismatch 2) Public keys are matched binary if available in both
 * keys; only one key having public key material will NOT be considered a
 * mismatch if both private keys are present and match: The latter logic will
 *    only be triggered if domain parameter matching is requested to
 * distinguish between a pure-play public key match/test and one checking
 * OpenSSL-type "EVP-PKEY-equality". This is possible as domain parameters
 * don't really play a role in OQS, so we consider them as a proxy for private
 * key matching.
 */

static int oqsx_match(const void *keydata1, const void *keydata2,
                      int selection) {
    const OQSX_KEY *key1 = keydata1;
    const OQSX_KEY *key2 = keydata2;
    int ok = 1;

    OQS_KM_PRINTF3("OQSKEYMGMT: match called for %p and %p\n", keydata1,
                   keydata2);
    OQS_KM_PRINTF2("OQSKEYMGMT: match called for selection %d\n", selection);

    if (key1 == NULL || key2 == NULL) {
        ERR_raise(ERR_LIB_USER, OQSPROV_R_WRONG_PARAMETERS);
        return 0;
    }

#ifdef NOPUBKEY_IN_PRIVKEY
    /* Now this is a "leap of faith" logic: If a public-only PKEY and a
     * private-only PKEY are tested for equality we cannot do anything other
     * than saying OK (as per
     * https://github.com/PQClean/PQClean/issues/415#issuecomment-910377682) if
     * at least the key type name matches. Potential actual key mismatches will
     * only be discovered later.
     */
    if (((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0) &&
        ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0)) {
        if ((key1->privkey == NULL && key2->pubkey == NULL) ||
            (key1->pubkey == NULL && key2->privkey == NULL) ||
            ((key1->tls_name != NULL && key2->tls_name != NULL) &&
             !strcmp(key1->tls_name, key2->tls_name))) {
            OQS_KM_PRINTF("OQSKEYMGMT: leap-of-faith match\n");
            return 1;
        }
    }
#endif

    if (((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0) &&
        ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) == 0)) {
        if ((key1->privkey == NULL && key2->privkey != NULL) ||
            (key1->privkey != NULL && key2->privkey == NULL) ||
            ((key1->tls_name != NULL && key2->tls_name != NULL) &&
             strcmp(key1->tls_name, key2->tls_name))) {
            ok = 0;
        } else {
            ok = ((key1->privkey == NULL && key2->privkey == NULL) ||
                  ((key1->privkey != NULL) &&
                   CRYPTO_memcmp(key1->privkey, key2->privkey,
                                 key1->privkeylen) == 0));
        }
    }

    if ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0) {
        if ((key1->pubkey == NULL && key2->pubkey != NULL) ||
            (key1->pubkey != NULL && key2->pubkey == NULL) ||
            ((key1->tls_name != NULL && key2->tls_name != NULL) &&
             strcmp(key1->tls_name, key2->tls_name))) {
            // special case now: If domain parameter matching
            // requested, consider private key match sufficient:
            ok = ((selection & OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS) != 0) &&
                 (key1->privkey != NULL && key2->privkey != NULL) &&
                 (CRYPTO_memcmp(key1->privkey, key2->privkey,
                                key1->privkeylen) == 0);
        } else {
            ok = ok && ((key1->pubkey == NULL && key2->pubkey == NULL) ||
                        ((key1->pubkey != NULL) &&
                         CRYPTO_memcmp(key1->pubkey, key2->pubkey,
                                       key1->pubkeylen) == 0));
        }
    }
    if (!ok)
        OQS_KM_PRINTF("OQSKEYMGMT: match failed!\n");
    return ok;
}

static int oqsx_import(void *keydata, int selection,
                       const OSSL_PARAM params[]) {
    OQSX_KEY *key = keydata;
    int ok = 0;

    OQS_KM_PRINTF("OQSKEYMGMT: import called \n");
    if (key == NULL) {
        ERR_raise(ERR_LIB_USER, OQSPROV_UNEXPECTED_NULL);
        return ok;
    }

    if (((selection & OSSL_KEYMGMT_SELECT_ALL_PARAMETERS) != 0) &&
        (oqsx_key_fromdata(key, params, 1)))
        ok = 1;
    return ok;
}

int oqsx_key_to_params(const OQSX_KEY *key, OSSL_PARAM_BLD *tmpl,
                       OSSL_PARAM params[], int include_private) {
    int ret = 0;

    if (key == NULL)
        return 0;

    if (key->pubkey != NULL) {
        OSSL_PARAM *p = NULL;

        if (tmpl == NULL) {
            p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PUB_KEY);
        }

        if (p != NULL || tmpl != NULL) {
            if (key->pubkeylen == 0 || !oqsx_param_build_set_octet_string(
                                           tmpl, p, OSSL_PKEY_PARAM_PUB_KEY,
                                           key->pubkey, key->pubkeylen))
                goto err;
        }
    }
    if (key->privkey != NULL && include_private) {
        OSSL_PARAM *p = NULL;

        /*
         * Key import/export should never leak the bit length of the secret
         * scalar in the key. Conceptually. OQS is not production strength
         * so does not care. TBD.
         *
         */

        if (tmpl == NULL) {
            p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PRIV_KEY);
        }

        if (p != NULL || tmpl != NULL) {
            if (key->privkeylen == 0 || !oqsx_param_build_set_octet_string(
                                            tmpl, p, OSSL_PKEY_PARAM_PRIV_KEY,
                                            key->privkey, key->privkeylen))
                goto err;
        }
    }
    // not passing in params to respond to is no error; the response is empty
    ret = 1;
err:
    return ret;
}

static int oqsx_export(void *keydata, int selection, OSSL_CALLBACK *param_cb,
                       void *cbarg) {
    OQSX_KEY *key = keydata;
    OSSL_PARAM_BLD *tmpl;
    OSSL_PARAM *params = NULL;
    OSSL_PARAM *p;
    int ok = 1;

    OQS_KM_PRINTF("OQSKEYMGMT: export called\n");

    /*
     * In this implementation, only public and private keys can be exported,
     * nothing else
     */
    if (key == NULL || param_cb == NULL) {
        ERR_raise(ERR_LIB_USER, OQSPROV_R_WRONG_PARAMETERS);
        return 0;
    }

    tmpl = OSSL_PARAM_BLD_new();
    if (tmpl == NULL) {
        ERR_raise(ERR_LIB_USER, OQSPROV_UNEXPECTED_NULL);
        return 0;
    }

    if ((selection & OSSL_KEYMGMT_SELECT_KEYPAIR) != 0) {
        int include_private =
            selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY ? 1 : 0;

        ok = ok && oqsx_key_to_params(key, tmpl, NULL, include_private);
    }

    params = OSSL_PARAM_BLD_to_param(tmpl);
    if (params == NULL) {
        ok = 0;
        goto err;
    }

    ok = ok & param_cb(params, cbarg);
    OSSL_PARAM_free(params);
err:
    OSSL_PARAM_BLD_free(tmpl);
    return ok;
}

#define OQS_HYBRID_KEY_TYPES()                                                 \
    OSSL_PARAM_octet_string(OQS_HYBRID_PKEY_PARAM_CLASSICAL_PUB_KEY, NULL, 0), \
        OSSL_PARAM_octet_string(OQS_HYBRID_PKEY_PARAM_CLASSICAL_PRIV_KEY,      \
                                NULL, 0),                                      \
        OSSL_PARAM_octet_string(OQS_HYBRID_PKEY_PARAM_PQ_PUB_KEY, NULL, 0),    \
        OSSL_PARAM_octet_string(OQS_HYBRID_PKEY_PARAM_PQ_PRIV_KEY, NULL, 0)

#define OQS_KEY_TYPES()                                                        \
    OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PUB_KEY, NULL, 0),                 \
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PRIV_KEY, NULL, 0),            \
        OQS_HYBRID_KEY_TYPES()

static const OSSL_PARAM oqsx_key_types[] = {OQS_KEY_TYPES(), OSSL_PARAM_END};
static const OSSL_PARAM *oqs_imexport_types(int selection) {
    OQS_KM_PRINTF("OQSKEYMGMT: imexport called\n");
    if ((selection & OSSL_KEYMGMT_SELECT_KEYPAIR) != 0)
        return oqsx_key_types;
    return NULL;
}

// Tells if a key (SIG, KEM, ECP_HYB_KEM, ECX_HYB_KEM or HYB_SIG) is using
// hybrid algorithm.
//
// Returns 1 if hybrid, else 0.
static int oqsx_key_is_hybrid(const OQSX_KEY *oqsxk) {
    if ((oqsxk->keytype == KEY_TYPE_ECP_HYB_KEM ||
         oqsxk->keytype == KEY_TYPE_ECX_HYB_KEM ||
         oqsxk->keytype == KEY_TYPE_HYB_SIG) &&
        oqsxk->numkeys == 2 && oqsxk->classical_pkey != NULL) {
        OQS_KM_PRINTF("OQSKEYMGMT: key is hybrid\n");
        return 1;
    }
    return 0;
}

// Gets the classical params of an hybrid key.

// Gets hybrid params.
//
// Returns 0 on success.
static int oqsx_get_hybrid_params(OQSX_KEY *key, OSSL_PARAM params[]) {
    OSSL_PARAM *p;
    const void *classical_pubkey = NULL;
    const void *classical_privkey = NULL;
    const void *pq_pubkey = NULL;
    const void *pq_privkey = NULL;
    uint32_t classical_pubkey_len = 0;
    uint32_t classical_privkey_len = 0;
    int pq_pubkey_len = 0;
    int pq_privkey_len = 0;
    int idx_classic, idx_pq;

    if (oqsx_key_is_hybrid(key) != 1)
        return 0;

    if (key->numkeys != 2) {
        OQS_KM_PRINTF2("OQSKEYMGMT: key is hybrid but key->numkeys = %zu\n",
                       key->numkeys);
        ERR_raise(ERR_LIB_PROV, OQSPROV_R_INTERNAL_ERROR);
        return -1;
    }

    oqsx_comp_set_idx(key, &idx_classic, &idx_pq);

    if (key->comp_pubkey != NULL && key->pubkey != NULL &&
        key->comp_pubkey[idx_classic] != NULL) {
        classical_pubkey = key->comp_pubkey[idx_classic];
        DECODE_UINT32(classical_pubkey_len, key->pubkey);
    }
    if (key->comp_privkey != NULL && key->privkey != NULL &&
        key->comp_privkey[idx_classic] != NULL) {
        classical_privkey = key->comp_privkey[idx_classic];
        DECODE_UINT32(classical_privkey_len, key->privkey);
    }

    if (key->comp_pubkey != NULL && key->comp_pubkey[idx_pq] != NULL) {
        pq_pubkey = key->comp_pubkey[idx_pq];
        pq_pubkey_len = key->pubkeylen - classical_pubkey_len - SIZE_OF_UINT32;
    }
    if (key->comp_privkey != NULL && key->comp_privkey != NULL) {
        pq_privkey = key->comp_privkey[idx_pq];
        pq_privkey_len =
            key->privkeylen - classical_privkey_len - SIZE_OF_UINT32;
    }

    if ((p = OSSL_PARAM_locate(
             params, OQS_HYBRID_PKEY_PARAM_CLASSICAL_PUB_KEY)) != NULL &&
        !OSSL_PARAM_set_octet_string(p, classical_pubkey, classical_pubkey_len))
        return -1;
    if ((p = OSSL_PARAM_locate(
             params, OQS_HYBRID_PKEY_PARAM_CLASSICAL_PRIV_KEY)) != NULL &&
        !OSSL_PARAM_set_octet_string(p, classical_privkey,
                                     classical_privkey_len))
        return -1;
    if ((p = OSSL_PARAM_locate(params, OQS_HYBRID_PKEY_PARAM_PQ_PUB_KEY)) !=
            NULL &&
        !OSSL_PARAM_set_octet_string(p, pq_pubkey, pq_pubkey_len))
        return -1;
    if ((p = OSSL_PARAM_locate(params, OQS_HYBRID_PKEY_PARAM_PQ_PRIV_KEY)) !=
            NULL &&
        !OSSL_PARAM_set_octet_string(p, pq_privkey, pq_privkey_len))
        return -1;

    return 0;
}

// must handle param requests for KEM and SIG keys...
static int oqsx_get_params(void *key, OSSL_PARAM params[]) {
    OQSX_KEY *oqsxk = key;
    OSSL_PARAM *p;

    if (oqsxk == NULL || params == NULL) {
        ERR_raise(ERR_LIB_USER, OQSPROV_R_WRONG_PARAMETERS);
        return 0;
    }

    OQS_KM_PRINTF2("OQSKEYMGMT: get_params called for %s\n", params[0].key);
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_BITS)) != NULL &&
        !OSSL_PARAM_set_int(p, oqsx_key_secbits(oqsxk)))
        return 0;
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_SECURITY_BITS)) !=
            NULL &&
        !OSSL_PARAM_set_int(p, oqsx_key_secbits(oqsxk)))
        return 0;
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_MAX_SIZE)) != NULL &&
        !OSSL_PARAM_set_int(p, oqsx_key_maxsize(oqsxk)))
        return 0;

    /* add as temporary workaround TBC */
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_DEFAULT_DIGEST)) !=
            NULL &&
        !OSSL_PARAM_set_utf8_string(p, SN_undef))
        return 0;
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_MANDATORY_DIGEST)) !=
            NULL &&
        !OSSL_PARAM_set_utf8_string(p, SN_undef))
        return 0;
    /* end workaround */

    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY)) !=
        NULL) {
        // hybrid KEMs are special in that the classic length information
        // shall not be passed out:
        if (oqsxk->keytype == KEY_TYPE_ECP_HYB_KEM ||
            oqsxk->keytype == KEY_TYPE_ECX_HYB_KEM) {
            if (!OSSL_PARAM_set_octet_string(
                    p, (char *)oqsxk->pubkey + SIZE_OF_UINT32,
                    oqsxk->pubkeylen - SIZE_OF_UINT32))
                return 0;
        } else {
            if (!OSSL_PARAM_set_octet_string(p, oqsxk->pubkey,
                                             oqsxk->pubkeylen))
                return 0;
        }
    }
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PUB_KEY)) != NULL) {
        if (!OSSL_PARAM_set_octet_string(p, oqsxk->pubkey, oqsxk->pubkeylen))
            return 0;
    }
    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_PRIV_KEY)) != NULL) {
        if (!OSSL_PARAM_set_octet_string(p, oqsxk->privkey, oqsxk->privkeylen))
            return 0;
    }

    if (oqsx_get_hybrid_params(oqsxk, params))
        return 0;

    // not passing in params to respond to is no error
    return 1;
}

static const OSSL_PARAM oqsx_gettable_params[] = {
    OSSL_PARAM_int(OSSL_PKEY_PARAM_BITS, NULL),
    OSSL_PARAM_int(OSSL_PKEY_PARAM_SECURITY_BITS, NULL),
    OSSL_PARAM_int(OSSL_PKEY_PARAM_MAX_SIZE, NULL),
    OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, NULL, 0),
    OQS_KEY_TYPES(),
    OSSL_PARAM_END};

static const OSSL_PARAM *oqs_gettable_params(void *provctx) {
    OQS_KM_PRINTF("OQSKEYMGMT: gettable_params called\n");
    return oqsx_gettable_params;
}

static int set_property_query(OQSX_KEY *oqsxkey, const char *propq) {
    OPENSSL_free(oqsxkey->propq);
    oqsxkey->propq = NULL;
    OQS_KM_PRINTF("OQSKEYMGMT: property_query called\n");
    if (propq != NULL) {
        oqsxkey->propq = OPENSSL_strdup(propq);
        if (oqsxkey->propq == NULL) {
            ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
            return 0;
        }
    }
    return 1;
}

static int oqsx_set_params(void *key, const OSSL_PARAM params[]) {
    OQSX_KEY *oqsxkey = key;
    const OSSL_PARAM *p;

    OQS_KM_PRINTF("OQSKEYMGMT: set_params called\n");
    if (oqsxkey == NULL) {
        ERR_raise(ERR_LIB_USER, OQSPROV_R_WRONG_PARAMETERS);
        return 0;
    }
    p = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY);
    if (p != NULL) {
        size_t used_len;
        int classic_pubkey_len;
        if (oqsxkey->keytype == KEY_TYPE_ECP_HYB_KEM ||
            oqsxkey->keytype == KEY_TYPE_ECX_HYB_KEM) {
            // classic key len already stored by key setup; only data
            // needs to be filled in
            if (p->data_size != oqsxkey->pubkeylen - SIZE_OF_UINT32 ||
                !OSSL_PARAM_get_octet_string(
                    p, &oqsxkey->comp_pubkey[0],
                    oqsxkey->pubkeylen - SIZE_OF_UINT32, &used_len)) {
                return 0;
            }
        } else {
            if (p->data_size != oqsxkey->pubkeylen ||
                !OSSL_PARAM_get_octet_string(p, &oqsxkey->pubkey,
                                             oqsxkey->pubkeylen, &used_len)) {
                return 0;
            }
        }
        OPENSSL_clear_free(oqsxkey->privkey, oqsxkey->privkeylen);
        oqsxkey->privkey = NULL;
    }
    p = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_PROPERTIES);
    if (p != NULL) {
        if (p->data_type != OSSL_PARAM_UTF8_STRING ||
            !set_property_query(oqsxkey, p->data)) {
            return 0;
        }
    }

    // not passing in params to set is no error, just a no-op
    return 1;
}

static const OSSL_PARAM oqs_settable_params[] = {
    OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, NULL, 0),
    OSSL_PARAM_utf8_string(OSSL_PKEY_PARAM_PROPERTIES, NULL, 0),
    OSSL_PARAM_END};

static const OSSL_PARAM *oqsx_settable_params(void *provctx) {
    OQS_KM_PRINTF("OQSKEYMGMT: settable_params called\n");
    return oqs_settable_params;
}

static void *oqsx_gen_init(void *provctx, int selection, char *oqs_name,
                           char *tls_name, int primitive, int bit_security,
                           int alg_idx, int reverse_share) {
    OSSL_LIB_CTX *libctx = PROV_OQS_LIBCTX_OF(provctx);
    struct oqsx_gen_ctx *gctx = NULL;

    OQS_KM_PRINTF2("OQSKEYMGMT: gen_init called for key %s \n", oqs_name);

    if ((gctx = OPENSSL_zalloc(sizeof(*gctx))) != NULL) {
        gctx->libctx = libctx;
        gctx->oqs_name = OPENSSL_strdup(oqs_name);
        gctx->tls_name = OPENSSL_strdup(tls_name);
        gctx->primitive = primitive;
        gctx->selection = selection;
        gctx->bit_security = bit_security;
        gctx->alg_idx = alg_idx;
        gctx->reverse_share = reverse_share;
    }
    return gctx;
}

static void *oqsx_genkey(struct oqsx_gen_ctx *gctx) {
    OQSX_KEY *key;

    if (gctx == NULL)
        return NULL;
    OQS_KM_PRINTF3("OQSKEYMGMT: gen called for %s (%s)\n", gctx->oqs_name,
                   gctx->tls_name);
    if ((key = oqsx_key_new(gctx->libctx, gctx->oqs_name, gctx->tls_name,
                            gctx->primitive, gctx->propq, gctx->bit_security,
                            gctx->alg_idx, gctx->reverse_share)) == NULL) {
        OQS_KM_PRINTF2("OQSKM: Error generating key for %s\n", gctx->tls_name);
        ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    if (oqsx_key_gen(key)) {
        ERR_raise(ERR_LIB_USER, OQSPROV_UNEXPECTED_NULL);
        return NULL;
    }
    return key;
}

static void *oqsx_gen(void *genctx, OSSL_CALLBACK *osslcb, void *cbarg) {
    struct oqsx_gen_ctx *gctx = genctx;

    OQS_KM_PRINTF("OQSKEYMGMT: gen called\n");

    return oqsx_genkey(gctx);
}

static void oqsx_gen_cleanup(void *genctx) {
    struct oqsx_gen_ctx *gctx = genctx;

    OQS_KM_PRINTF("OQSKEYMGMT: gen_cleanup called\n");
    OPENSSL_free(gctx->oqs_name);
    OPENSSL_free(gctx->tls_name);
    OPENSSL_free(gctx->propq);
    OPENSSL_free(gctx);
}

void *oqsx_load(const void *reference, size_t reference_sz) {
    OQSX_KEY *key = NULL;

    OQS_KM_PRINTF("OQSKEYMGMT: load called\n");
    if (reference_sz == sizeof(key)) {
        /* The contents of the reference is the address to our object */
        key = *(OQSX_KEY **)reference;
        /* We grabbed, so we detach it */
        *(OQSX_KEY **)reference = NULL;
        return key;
    }
    return NULL;
}

static const OSSL_PARAM *oqsx_gen_settable_params(void *provctx) {
    static OSSL_PARAM settable[] = {
        OSSL_PARAM_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, NULL, 0),
        OSSL_PARAM_utf8_string(OSSL_KDF_PARAM_PROPERTIES, NULL, 0),
        OSSL_PARAM_END};
    return settable;
}

static int oqsx_gen_set_params(void *genctx, const OSSL_PARAM params[]) {
    struct oqsx_gen_ctx *gctx = genctx;
    const OSSL_PARAM *p;

    OQS_KM_PRINTF("OQSKEYMGMT: gen_set_params called\n");
    if (gctx == NULL)
        return 0;

    p = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_GROUP_NAME);
    if (p != NULL) {
        const char *algname = (char *)p->data;

        OPENSSL_free(gctx->tls_name);
        gctx->tls_name = OPENSSL_strdup(algname);
    }
    p = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_PROPERTIES);
    if (p != NULL) {
        if (p->data_type != OSSL_PARAM_UTF8_STRING)
            return 0;
        OPENSSL_free(gctx->propq);
        gctx->propq = OPENSSL_strdup(p->data);
        if (gctx->propq == NULL)
            return 0;
    }
    // not passing in params is no error; subsequent operations may fail,
    // though
    return 1;
}

///// OQS_TEMPLATE_FRAGMENT_KEYMGMT_CONSTRUCTORS_START

static void *mldsa44_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_44,
                        "mldsa44", KEY_TYPE_SIG, NULL, 128, 0, 0);
}

static void *mldsa44_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_44, "mldsa44",
                         0, 128, 0, 0);
}
static void *p256_mldsa44_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_44,
                        "p256_mldsa44", KEY_TYPE_HYB_SIG, NULL, 128, 1, 0);
}

static void *p256_mldsa44_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_44,
                         "p256_mldsa44", KEY_TYPE_HYB_SIG, 128, 1, 0);
}
static void *rsa3072_mldsa44_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_44,
                        "rsa3072_mldsa44", KEY_TYPE_HYB_SIG, NULL, 128, 2, 0);
}

static void *rsa3072_mldsa44_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_44,
                         "rsa3072_mldsa44", KEY_TYPE_HYB_SIG, 128, 2, 0);
}
static void *mldsa65_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_65,
                        "mldsa65", KEY_TYPE_SIG, NULL, 192, 3, 0);
}

static void *mldsa65_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_65, "mldsa65",
                         0, 192, 3, 0);
}
static void *p384_mldsa65_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_65,
                        "p384_mldsa65", KEY_TYPE_HYB_SIG, NULL, 192, 4, 0);
}

static void *p384_mldsa65_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_65,
                         "p384_mldsa65", KEY_TYPE_HYB_SIG, 192, 4, 0);
}
static void *mldsa87_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_87,
                        "mldsa87", KEY_TYPE_SIG, NULL, 256, 5, 0);
}

static void *mldsa87_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_87, "mldsa87",
                         0, 256, 5, 0);
}
static void *p521_mldsa87_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_ml_dsa_87,
                        "p521_mldsa87", KEY_TYPE_HYB_SIG, NULL, 256, 6, 0);
}

static void *p521_mldsa87_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_ml_dsa_87,
                         "p521_mldsa87", KEY_TYPE_HYB_SIG, 256, 6, 0);
}

static void *falcon512_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_falcon_512,
                        "falcon512", KEY_TYPE_SIG, NULL, 128, 7, 0);
}

static void *falcon512_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_512,
                         "falcon512", 0, 128, 7, 0);
}
static void *p256_falcon512_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_falcon_512,
                        "p256_falcon512", KEY_TYPE_HYB_SIG, NULL, 128, 8, 0);
}

static void *p256_falcon512_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_512,
                         "p256_falcon512", KEY_TYPE_HYB_SIG, 128, 8, 0);
}
static void *rsa3072_falcon512_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_falcon_512,
                        "rsa3072_falcon512", KEY_TYPE_HYB_SIG, NULL, 128, 9, 0);
}

static void *rsa3072_falcon512_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_512,
                         "rsa3072_falcon512", KEY_TYPE_HYB_SIG, 128, 9, 0);
}
static void *falconpadded512_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_falcon_padded_512, "falconpadded512",
                        KEY_TYPE_SIG, NULL, 128, 10, 0);
}

static void *falconpadded512_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_padded_512,
                         "falconpadded512", 0, 128, 10, 0);
}
static void *p256_falconpadded512_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_falcon_padded_512, "p256_falconpadded512",
                        KEY_TYPE_HYB_SIG, NULL, 128, 11, 0);
}

static void *p256_falconpadded512_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_padded_512,
                         "p256_falconpadded512", KEY_TYPE_HYB_SIG, 128, 11, 0);
}
static void *rsa3072_falconpadded512_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_falcon_padded_512,
        "rsa3072_falconpadded512", KEY_TYPE_HYB_SIG, NULL, 128, 12, 0);
}

static void *rsa3072_falconpadded512_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_padded_512,
                         "rsa3072_falconpadded512", KEY_TYPE_HYB_SIG, 128, 12,
                         0);
}
static void *falcon1024_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_falcon_1024,
                        "falcon1024", KEY_TYPE_SIG, NULL, 256, 13, 0);
}

static void *falcon1024_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_1024,
                         "falcon1024", 0, 256, 13, 0);
}
static void *p521_falcon1024_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_falcon_1024,
                        "p521_falcon1024", KEY_TYPE_HYB_SIG, NULL, 256, 14, 0);
}

static void *p521_falcon1024_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_1024,
                         "p521_falcon1024", KEY_TYPE_HYB_SIG, 256, 14, 0);
}
static void *falconpadded1024_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_falcon_padded_1024, "falconpadded1024",
                        KEY_TYPE_SIG, NULL, 256, 15, 0);
}

static void *falconpadded1024_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_padded_1024,
                         "falconpadded1024", 0, 256, 15, 0);
}
static void *p521_falconpadded1024_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_falcon_padded_1024, "p521_falconpadded1024",
                        KEY_TYPE_HYB_SIG, NULL, 256, 16, 0);
}

static void *p521_falconpadded1024_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_falcon_padded_1024,
                         "p521_falconpadded1024", KEY_TYPE_HYB_SIG, 256, 16, 0);
}

static void *sphincssha2128fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_128f_simple,
        "sphincssha2128fsimple", KEY_TYPE_SIG, NULL, 128, 17, 0);
}

static void *sphincssha2128fsimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection,
                         OQS_SIG_alg_sphincs_sha2_128f_simple,
                         "sphincssha2128fsimple", 0, 128, 17, 0);
}
static void *p256_sphincssha2128fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_128f_simple,
        "p256_sphincssha2128fsimple", KEY_TYPE_HYB_SIG, NULL, 128, 18, 0);
}

static void *p256_sphincssha2128fsimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_sha2_128f_simple,
        "p256_sphincssha2128fsimple", KEY_TYPE_HYB_SIG, 128, 18, 0);
}
static void *rsa3072_sphincssha2128fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_128f_simple,
        "rsa3072_sphincssha2128fsimple", KEY_TYPE_HYB_SIG, NULL, 128, 19, 0);
}

static void *rsa3072_sphincssha2128fsimple_gen_init(void *provctx,
                                                    int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_sha2_128f_simple,
        "rsa3072_sphincssha2128fsimple", KEY_TYPE_HYB_SIG, 128, 19, 0);
}
static void *sphincssha2128ssimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_128s_simple,
        "sphincssha2128ssimple", KEY_TYPE_SIG, NULL, 128, 20, 0);
}

static void *sphincssha2128ssimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection,
                         OQS_SIG_alg_sphincs_sha2_128s_simple,
                         "sphincssha2128ssimple", 0, 128, 20, 0);
}
static void *p256_sphincssha2128ssimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_128s_simple,
        "p256_sphincssha2128ssimple", KEY_TYPE_HYB_SIG, NULL, 128, 21, 0);
}

static void *p256_sphincssha2128ssimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_sha2_128s_simple,
        "p256_sphincssha2128ssimple", KEY_TYPE_HYB_SIG, 128, 21, 0);
}
static void *rsa3072_sphincssha2128ssimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_128s_simple,
        "rsa3072_sphincssha2128ssimple", KEY_TYPE_HYB_SIG, NULL, 128, 22, 0);
}

static void *rsa3072_sphincssha2128ssimple_gen_init(void *provctx,
                                                    int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_sha2_128s_simple,
        "rsa3072_sphincssha2128ssimple", KEY_TYPE_HYB_SIG, 128, 22, 0);
}
static void *sphincssha2192fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_192f_simple,
        "sphincssha2192fsimple", KEY_TYPE_SIG, NULL, 192, 23, 0);
}

static void *sphincssha2192fsimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection,
                         OQS_SIG_alg_sphincs_sha2_192f_simple,
                         "sphincssha2192fsimple", 0, 192, 23, 0);
}
static void *p384_sphincssha2192fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_sha2_192f_simple,
        "p384_sphincssha2192fsimple", KEY_TYPE_HYB_SIG, NULL, 192, 24, 0);
}

static void *p384_sphincssha2192fsimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_sha2_192f_simple,
        "p384_sphincssha2192fsimple", KEY_TYPE_HYB_SIG, 192, 24, 0);
}

static void *sphincsshake128fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_shake_128f_simple,
        "sphincsshake128fsimple", KEY_TYPE_SIG, NULL, 128, 25, 0);
}

static void *sphincsshake128fsimple_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection,
                         OQS_SIG_alg_sphincs_shake_128f_simple,
                         "sphincsshake128fsimple", 0, 128, 25, 0);
}
static void *p256_sphincsshake128fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_shake_128f_simple,
        "p256_sphincsshake128fsimple", KEY_TYPE_HYB_SIG, NULL, 128, 26, 0);
}

static void *p256_sphincsshake128fsimple_gen_init(void *provctx,
                                                  int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_shake_128f_simple,
        "p256_sphincsshake128fsimple", KEY_TYPE_HYB_SIG, 128, 26, 0);
}
static void *rsa3072_sphincsshake128fsimple_new_key(void *provctx) {
    return oqsx_key_new(
        PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_sphincs_shake_128f_simple,
        "rsa3072_sphincsshake128fsimple", KEY_TYPE_HYB_SIG, NULL, 128, 27, 0);
}

static void *rsa3072_sphincsshake128fsimple_gen_init(void *provctx,
                                                     int selection) {
    return oqsx_gen_init(
        provctx, selection, OQS_SIG_alg_sphincs_shake_128f_simple,
        "rsa3072_sphincsshake128fsimple", KEY_TYPE_HYB_SIG, 128, 27, 0);
}

static void *mayo1_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_1,
                        "mayo1", KEY_TYPE_SIG, NULL, 128, 28, 0);
}

static void *mayo1_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_1, "mayo1", 0,
                         128, 28, 0);
}
static void *p256_mayo1_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_1,
                        "p256_mayo1", KEY_TYPE_HYB_SIG, NULL, 128, 29, 0);
}

static void *p256_mayo1_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_1, "p256_mayo1",
                         KEY_TYPE_HYB_SIG, 128, 29, 0);
}
static void *mayo2_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_2,
                        "mayo2", KEY_TYPE_SIG, NULL, 128, 30, 0);
}

static void *mayo2_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_2, "mayo2", 0,
                         128, 30, 0);
}
static void *p256_mayo2_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_2,
                        "p256_mayo2", KEY_TYPE_HYB_SIG, NULL, 128, 31, 0);
}

static void *p256_mayo2_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_2, "p256_mayo2",
                         KEY_TYPE_HYB_SIG, 128, 31, 0);
}
static void *mayo3_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_3,
                        "mayo3", KEY_TYPE_SIG, NULL, 192, 32, 0);
}

static void *mayo3_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_3, "mayo3", 0,
                         192, 32, 0);
}
static void *p384_mayo3_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_3,
                        "p384_mayo3", KEY_TYPE_HYB_SIG, NULL, 192, 33, 0);
}

static void *p384_mayo3_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_3, "p384_mayo3",
                         KEY_TYPE_HYB_SIG, 192, 33, 0);
}
static void *mayo5_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_5,
                        "mayo5", KEY_TYPE_SIG, NULL, 256, 34, 0);
}

static void *mayo5_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_5, "mayo5", 0,
                         256, 34, 0);
}
static void *p521_mayo5_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_mayo_5,
                        "p521_mayo5", KEY_TYPE_HYB_SIG, NULL, 256, 35, 0);
}

static void *p521_mayo5_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_mayo_5, "p521_mayo5",
                         KEY_TYPE_HYB_SIG, 256, 35, 0);
}

static void *CROSSrsdp128balanced_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_cross_rsdp_128_balanced,
                        "CROSSrsdp128balanced", KEY_TYPE_SIG, NULL, 128, 36, 0);
}

static void *CROSSrsdp128balanced_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection,
                         OQS_SIG_alg_cross_rsdp_128_balanced,
                         "CROSSrsdp128balanced", 0, 128, 36, 0);
}

static void *OV_Is_pkc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_uov_ov_Is_pkc,
                        "OV_Is_pkc", KEY_TYPE_SIG, NULL, 128, 37, 0);
}

static void *OV_Is_pkc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Is_pkc,
                         "OV_Is_pkc", 0, 128, 37, 0);
}
static void *p256_OV_Is_pkc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_uov_ov_Is_pkc,
                        "p256_OV_Is_pkc", KEY_TYPE_HYB_SIG, NULL, 128, 38, 0);
}

static void *p256_OV_Is_pkc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Is_pkc,
                         "p256_OV_Is_pkc", KEY_TYPE_HYB_SIG, 128, 38, 0);
}
static void *OV_Ip_pkc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_uov_ov_Ip_pkc,
                        "OV_Ip_pkc", KEY_TYPE_SIG, NULL, 128, 39, 0);
}

static void *OV_Ip_pkc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Ip_pkc,
                         "OV_Ip_pkc", 0, 128, 39, 0);
}
static void *p256_OV_Ip_pkc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), OQS_SIG_alg_uov_ov_Ip_pkc,
                        "p256_OV_Ip_pkc", KEY_TYPE_HYB_SIG, NULL, 128, 40, 0);
}

static void *p256_OV_Ip_pkc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Ip_pkc,
                         "p256_OV_Ip_pkc", KEY_TYPE_HYB_SIG, 128, 40, 0);
}
static void *OV_Is_pkc_skc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_uov_ov_Is_pkc_skc, "OV_Is_pkc_skc",
                        KEY_TYPE_SIG, NULL, 128, 41, 0);
}

static void *OV_Is_pkc_skc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Is_pkc_skc,
                         "OV_Is_pkc_skc", 0, 128, 41, 0);
}
static void *p256_OV_Is_pkc_skc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_uov_ov_Is_pkc_skc, "p256_OV_Is_pkc_skc",
                        KEY_TYPE_HYB_SIG, NULL, 128, 42, 0);
}

static void *p256_OV_Is_pkc_skc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Is_pkc_skc,
                         "p256_OV_Is_pkc_skc", KEY_TYPE_HYB_SIG, 128, 42, 0);
}
static void *OV_Ip_pkc_skc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_uov_ov_Ip_pkc_skc, "OV_Ip_pkc_skc",
                        KEY_TYPE_SIG, NULL, 128, 43, 0);
}

static void *OV_Ip_pkc_skc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Ip_pkc_skc,
                         "OV_Ip_pkc_skc", 0, 128, 43, 0);
}
static void *p256_OV_Ip_pkc_skc_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_uov_ov_Ip_pkc_skc, "p256_OV_Ip_pkc_skc",
                        KEY_TYPE_HYB_SIG, NULL, 128, 44, 0);
}

static void *p256_OV_Ip_pkc_skc_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_uov_ov_Ip_pkc_skc,
                         "p256_OV_Ip_pkc_skc", KEY_TYPE_HYB_SIG, 128, 44, 0);
}

static void *snova2454_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_24_5_4, "snova2454",
                        KEY_TYPE_SIG, NULL, 128, 45, 0);
}

static void *snova2454_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_24_5_4,
                         "snova2454", 0, 128, 45, 0);
}
static void *p256_snova2454_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_24_5_4, "p256_snova2454",
                        KEY_TYPE_HYB_SIG, NULL, 128, 46, 0);
}

static void *p256_snova2454_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_24_5_4,
                         "p256_snova2454", KEY_TYPE_HYB_SIG, 128, 46, 0);
}
static void *snova2454esk_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_24_5_4_esk, "snova2454esk",
                        KEY_TYPE_SIG, NULL, 128, 47, 0);
}

static void *snova2454esk_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_24_5_4_esk,
                         "snova2454esk", 0, 128, 47, 0);
}
static void *p256_snova2454esk_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_24_5_4_esk, "p256_snova2454esk",
                        KEY_TYPE_HYB_SIG, NULL, 128, 48, 0);
}

static void *p256_snova2454esk_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_24_5_4_esk,
                         "p256_snova2454esk", KEY_TYPE_HYB_SIG, 128, 48, 0);
}
static void *snova37172_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_37_17_2, "snova37172",
                        KEY_TYPE_SIG, NULL, 128, 49, 0);
}

static void *snova37172_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_37_17_2,
                         "snova37172", 0, 128, 49, 0);
}
static void *p256_snova37172_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_37_17_2, "p256_snova37172",
                        KEY_TYPE_HYB_SIG, NULL, 128, 50, 0);
}

static void *p256_snova37172_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_37_17_2,
                         "p256_snova37172", KEY_TYPE_HYB_SIG, 128, 50, 0);
}
static void *snova2455_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_24_5_5, "snova2455",
                        KEY_TYPE_SIG, NULL, 192, 51, 0);
}

static void *snova2455_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_24_5_5,
                         "snova2455", 0, 192, 51, 0);
}
static void *p384_snova2455_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_24_5_5, "p384_snova2455",
                        KEY_TYPE_HYB_SIG, NULL, 192, 52, 0);
}

static void *p384_snova2455_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_24_5_5,
                         "p384_snova2455", KEY_TYPE_HYB_SIG, 192, 52, 0);
}
static void *snova2965_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_29_6_5, "snova2965",
                        KEY_TYPE_SIG, NULL, 256, 53, 0);
}

static void *snova2965_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_29_6_5,
                         "snova2965", 0, 256, 53, 0);
}
static void *p521_snova2965_new_key(void *provctx) {
    return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx),
                        OQS_SIG_alg_snova_SNOVA_29_6_5, "p521_snova2965",
                        KEY_TYPE_HYB_SIG, NULL, 256, 54, 0);
}

static void *p521_snova2965_gen_init(void *provctx, int selection) {
    return oqsx_gen_init(provctx, selection, OQS_SIG_alg_snova_SNOVA_29_6_5,
                         "p521_snova2965", KEY_TYPE_HYB_SIG, 256, 54, 0);
}

///// OQS_TEMPLATE_FRAGMENT_KEYMGMT_CONSTRUCTORS_END

#define MAKE_SIG_KEYMGMT_FUNCTIONS(alg)                                        \
                                                                               \
    const OSSL_DISPATCH oqs_##alg##_keymgmt_functions[] = {                    \
        {OSSL_FUNC_KEYMGMT_NEW, (void (*)(void))alg##_new_key},                \
        {OSSL_FUNC_KEYMGMT_FREE, (void (*)(void))oqsx_key_free},               \
        {OSSL_FUNC_KEYMGMT_GET_PARAMS, (void (*)(void))oqsx_get_params},       \
        {OSSL_FUNC_KEYMGMT_SETTABLE_PARAMS,                                    \
         (void (*)(void))oqsx_settable_params},                                \
        {OSSL_FUNC_KEYMGMT_GETTABLE_PARAMS,                                    \
         (void (*)(void))oqs_gettable_params},                                 \
        {OSSL_FUNC_KEYMGMT_SET_PARAMS, (void (*)(void))oqsx_set_params},       \
        {OSSL_FUNC_KEYMGMT_HAS, (void (*)(void))oqsx_has},                     \
        {OSSL_FUNC_KEYMGMT_MATCH, (void (*)(void))oqsx_match},                 \
        {OSSL_FUNC_KEYMGMT_IMPORT, (void (*)(void))oqsx_import},               \
        {OSSL_FUNC_KEYMGMT_IMPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_EXPORT, (void (*)(void))oqsx_export},               \
        {OSSL_FUNC_KEYMGMT_EXPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_GEN_INIT, (void (*)(void))alg##_gen_init},          \
        {OSSL_FUNC_KEYMGMT_GEN, (void (*)(void))oqsx_gen},                     \
        {OSSL_FUNC_KEYMGMT_GEN_CLEANUP, (void (*)(void))oqsx_gen_cleanup},     \
        {OSSL_FUNC_KEYMGMT_GEN_SET_PARAMS,                                     \
         (void (*)(void))oqsx_gen_set_params},                                 \
        {OSSL_FUNC_KEYMGMT_GEN_SETTABLE_PARAMS,                                \
         (void (*)(void))oqsx_gen_settable_params},                            \
        {OSSL_FUNC_KEYMGMT_LOAD, (void (*)(void))oqsx_load},                   \
        {0, NULL}};

#define MAKE_KEM_KEYMGMT_FUNCTIONS(tokalg, tokoqsalg, bit_security)            \
                                                                               \
    static void *tokalg##_new_key(void *provctx) {                             \
        return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), tokoqsalg,            \
                            "" #tokalg "", KEY_TYPE_KEM, NULL, bit_security,   \
                            -1, 0);                                            \
    }                                                                          \
                                                                               \
    static void *tokalg##_gen_init(void *provctx, int selection) {             \
        return oqsx_gen_init(provctx, selection, tokoqsalg, "" #tokalg "",     \
                             KEY_TYPE_KEM, bit_security, -1, 0);               \
    }                                                                          \
                                                                               \
    const OSSL_DISPATCH oqs_##tokalg##_keymgmt_functions[] = {                 \
        {OSSL_FUNC_KEYMGMT_NEW, (void (*)(void))tokalg##_new_key},             \
        {OSSL_FUNC_KEYMGMT_FREE, (void (*)(void))oqsx_key_free},               \
        {OSSL_FUNC_KEYMGMT_GET_PARAMS, (void (*)(void))oqsx_get_params},       \
        {OSSL_FUNC_KEYMGMT_SETTABLE_PARAMS,                                    \
         (void (*)(void))oqsx_settable_params},                                \
        {OSSL_FUNC_KEYMGMT_GETTABLE_PARAMS,                                    \
         (void (*)(void))oqs_gettable_params},                                 \
        {OSSL_FUNC_KEYMGMT_SET_PARAMS, (void (*)(void))oqsx_set_params},       \
        {OSSL_FUNC_KEYMGMT_HAS, (void (*)(void))oqsx_has},                     \
        {OSSL_FUNC_KEYMGMT_MATCH, (void (*)(void))oqsx_match},                 \
        {OSSL_FUNC_KEYMGMT_IMPORT, (void (*)(void))oqsx_import},               \
        {OSSL_FUNC_KEYMGMT_IMPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_EXPORT, (void (*)(void))oqsx_export},               \
        {OSSL_FUNC_KEYMGMT_EXPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_GEN_INIT, (void (*)(void))tokalg##_gen_init},       \
        {OSSL_FUNC_KEYMGMT_GEN, (void (*)(void))oqsx_gen},                     \
        {OSSL_FUNC_KEYMGMT_GEN_CLEANUP, (void (*)(void))oqsx_gen_cleanup},     \
        {OSSL_FUNC_KEYMGMT_GEN_SET_PARAMS,                                     \
         (void (*)(void))oqsx_gen_set_params},                                 \
        {OSSL_FUNC_KEYMGMT_GEN_SETTABLE_PARAMS,                                \
         (void (*)(void))oqsx_gen_settable_params},                            \
        {OSSL_FUNC_KEYMGMT_LOAD, (void (*)(void))oqsx_load},                   \
        {0, NULL}};

#define MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(tokalg, tokoqsalg, bit_security)        \
                                                                               \
    static void *ecp_##tokalg##_new_key(void *provctx) {                       \
        return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), tokoqsalg,            \
                            "" #tokalg "", KEY_TYPE_ECP_HYB_KEM, NULL,         \
                            bit_security, -1, 0);                              \
    }                                                                          \
                                                                               \
    static void *ecp_##tokalg##_gen_init(void *provctx, int selection) {       \
        return oqsx_gen_init(provctx, selection, tokoqsalg, "" #tokalg "",     \
                             KEY_TYPE_ECP_HYB_KEM, bit_security, -1, 0);       \
    }                                                                          \
                                                                               \
    const OSSL_DISPATCH oqs_ecp_##tokalg##_keymgmt_functions[] = {             \
        {OSSL_FUNC_KEYMGMT_NEW, (void (*)(void))ecp_##tokalg##_new_key},       \
        {OSSL_FUNC_KEYMGMT_FREE, (void (*)(void))oqsx_key_free},               \
        {OSSL_FUNC_KEYMGMT_GET_PARAMS, (void (*)(void))oqsx_get_params},       \
        {OSSL_FUNC_KEYMGMT_SETTABLE_PARAMS,                                    \
         (void (*)(void))oqsx_settable_params},                                \
        {OSSL_FUNC_KEYMGMT_GETTABLE_PARAMS,                                    \
         (void (*)(void))oqs_gettable_params},                                 \
        {OSSL_FUNC_KEYMGMT_SET_PARAMS, (void (*)(void))oqsx_set_params},       \
        {OSSL_FUNC_KEYMGMT_HAS, (void (*)(void))oqsx_has},                     \
        {OSSL_FUNC_KEYMGMT_MATCH, (void (*)(void))oqsx_match},                 \
        {OSSL_FUNC_KEYMGMT_IMPORT, (void (*)(void))oqsx_import},               \
        {OSSL_FUNC_KEYMGMT_IMPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_EXPORT, (void (*)(void))oqsx_export},               \
        {OSSL_FUNC_KEYMGMT_EXPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_GEN_INIT, (void (*)(void))ecp_##tokalg##_gen_init}, \
        {OSSL_FUNC_KEYMGMT_GEN, (void (*)(void))oqsx_gen},                     \
        {OSSL_FUNC_KEYMGMT_GEN_CLEANUP, (void (*)(void))oqsx_gen_cleanup},     \
        {OSSL_FUNC_KEYMGMT_GEN_SET_PARAMS,                                     \
         (void (*)(void))oqsx_gen_set_params},                                 \
        {OSSL_FUNC_KEYMGMT_GEN_SETTABLE_PARAMS,                                \
         (void (*)(void))oqsx_gen_settable_params},                            \
        {OSSL_FUNC_KEYMGMT_LOAD, (void (*)(void))oqsx_load},                   \
        {0, NULL}};

#define MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(tokalg, tokoqsalg, bit_security,        \
                                       pqfips)                                 \
    static void *ecx_##tokalg##_new_key(void *provctx) {                       \
        return oqsx_key_new(PROV_OQS_LIBCTX_OF(provctx), tokoqsalg,            \
                            "" #tokalg "", KEY_TYPE_ECX_HYB_KEM, NULL,         \
                            bit_security, -1, pqfips);                         \
    }                                                                          \
                                                                               \
    static void *ecx_##tokalg##_gen_init(void *provctx, int selection) {       \
        return oqsx_gen_init(provctx, selection, tokoqsalg, "" #tokalg "",     \
                             KEY_TYPE_ECX_HYB_KEM, bit_security, -1, pqfips);  \
    }                                                                          \
                                                                               \
    const OSSL_DISPATCH oqs_ecx_##tokalg##_keymgmt_functions[] = {             \
        {OSSL_FUNC_KEYMGMT_NEW, (void (*)(void))ecx_##tokalg##_new_key},       \
        {OSSL_FUNC_KEYMGMT_FREE, (void (*)(void))oqsx_key_free},               \
        {OSSL_FUNC_KEYMGMT_GET_PARAMS, (void (*)(void))oqsx_get_params},       \
        {OSSL_FUNC_KEYMGMT_SETTABLE_PARAMS,                                    \
         (void (*)(void))oqsx_settable_params},                                \
        {OSSL_FUNC_KEYMGMT_GETTABLE_PARAMS,                                    \
         (void (*)(void))oqs_gettable_params},                                 \
        {OSSL_FUNC_KEYMGMT_SET_PARAMS, (void (*)(void))oqsx_set_params},       \
        {OSSL_FUNC_KEYMGMT_HAS, (void (*)(void))oqsx_has},                     \
        {OSSL_FUNC_KEYMGMT_MATCH, (void (*)(void))oqsx_match},                 \
        {OSSL_FUNC_KEYMGMT_IMPORT, (void (*)(void))oqsx_import},               \
        {OSSL_FUNC_KEYMGMT_IMPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_EXPORT, (void (*)(void))oqsx_export},               \
        {OSSL_FUNC_KEYMGMT_EXPORT_TYPES, (void (*)(void))oqs_imexport_types},  \
        {OSSL_FUNC_KEYMGMT_GEN_INIT, (void (*)(void))ecx_##tokalg##_gen_init}, \
        {OSSL_FUNC_KEYMGMT_GEN, (void (*)(void))oqsx_gen},                     \
        {OSSL_FUNC_KEYMGMT_GEN_CLEANUP, (void (*)(void))oqsx_gen_cleanup},     \
        {OSSL_FUNC_KEYMGMT_GEN_SET_PARAMS,                                     \
         (void (*)(void))oqsx_gen_set_params},                                 \
        {OSSL_FUNC_KEYMGMT_GEN_SETTABLE_PARAMS,                                \
         (void (*)(void))oqsx_gen_settable_params},                            \
        {OSSL_FUNC_KEYMGMT_LOAD, (void (*)(void))oqsx_load},                   \
        {0, NULL}};

///// OQS_TEMPLATE_FRAGMENT_KEYMGMT_FUNCTIONS_START
MAKE_SIG_KEYMGMT_FUNCTIONS(mldsa44)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_mldsa44)
MAKE_SIG_KEYMGMT_FUNCTIONS(rsa3072_mldsa44)
MAKE_SIG_KEYMGMT_FUNCTIONS(mldsa65)
MAKE_SIG_KEYMGMT_FUNCTIONS(p384_mldsa65)
MAKE_SIG_KEYMGMT_FUNCTIONS(mldsa87)
MAKE_SIG_KEYMGMT_FUNCTIONS(p521_mldsa87)
MAKE_SIG_KEYMGMT_FUNCTIONS(falcon512)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_falcon512)
MAKE_SIG_KEYMGMT_FUNCTIONS(rsa3072_falcon512)
MAKE_SIG_KEYMGMT_FUNCTIONS(falconpadded512)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_falconpadded512)
MAKE_SIG_KEYMGMT_FUNCTIONS(rsa3072_falconpadded512)
MAKE_SIG_KEYMGMT_FUNCTIONS(falcon1024)
MAKE_SIG_KEYMGMT_FUNCTIONS(p521_falcon1024)
MAKE_SIG_KEYMGMT_FUNCTIONS(falconpadded1024)
MAKE_SIG_KEYMGMT_FUNCTIONS(p521_falconpadded1024)
MAKE_SIG_KEYMGMT_FUNCTIONS(sphincssha2128fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_sphincssha2128fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(rsa3072_sphincssha2128fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(sphincssha2128ssimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_sphincssha2128ssimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(rsa3072_sphincssha2128ssimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(sphincssha2192fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(p384_sphincssha2192fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(sphincsshake128fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_sphincsshake128fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(rsa3072_sphincsshake128fsimple)
MAKE_SIG_KEYMGMT_FUNCTIONS(mayo1)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_mayo1)
MAKE_SIG_KEYMGMT_FUNCTIONS(mayo2)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_mayo2)
MAKE_SIG_KEYMGMT_FUNCTIONS(mayo3)
MAKE_SIG_KEYMGMT_FUNCTIONS(p384_mayo3)
MAKE_SIG_KEYMGMT_FUNCTIONS(mayo5)
MAKE_SIG_KEYMGMT_FUNCTIONS(p521_mayo5)
MAKE_SIG_KEYMGMT_FUNCTIONS(CROSSrsdp128balanced)
MAKE_SIG_KEYMGMT_FUNCTIONS(OV_Is_pkc)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_OV_Is_pkc)
MAKE_SIG_KEYMGMT_FUNCTIONS(OV_Ip_pkc)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_OV_Ip_pkc)
MAKE_SIG_KEYMGMT_FUNCTIONS(OV_Is_pkc_skc)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_OV_Is_pkc_skc)
MAKE_SIG_KEYMGMT_FUNCTIONS(OV_Ip_pkc_skc)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_OV_Ip_pkc_skc)
MAKE_SIG_KEYMGMT_FUNCTIONS(snova2454)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_snova2454)
MAKE_SIG_KEYMGMT_FUNCTIONS(snova2454esk)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_snova2454esk)
MAKE_SIG_KEYMGMT_FUNCTIONS(snova37172)
MAKE_SIG_KEYMGMT_FUNCTIONS(p256_snova37172)
MAKE_SIG_KEYMGMT_FUNCTIONS(snova2455)
MAKE_SIG_KEYMGMT_FUNCTIONS(p384_snova2455)
MAKE_SIG_KEYMGMT_FUNCTIONS(snova2965)
MAKE_SIG_KEYMGMT_FUNCTIONS(p521_snova2965)

MAKE_KEM_KEYMGMT_FUNCTIONS(frodo640aes, OQS_KEM_alg_frodokem_640_aes, 128)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p256_frodo640aes, OQS_KEM_alg_frodokem_640_aes,
                               128)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x25519_frodo640aes, OQS_KEM_alg_frodokem_640_aes,
                               128, 0)
MAKE_KEM_KEYMGMT_FUNCTIONS(frodo640shake, OQS_KEM_alg_frodokem_640_shake, 128)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p256_frodo640shake,
                               OQS_KEM_alg_frodokem_640_shake, 128)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x25519_frodo640shake,
                               OQS_KEM_alg_frodokem_640_shake, 128, 0)
MAKE_KEM_KEYMGMT_FUNCTIONS(frodo976aes, OQS_KEM_alg_frodokem_976_aes, 192)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p384_frodo976aes, OQS_KEM_alg_frodokem_976_aes,
                               192)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x448_frodo976aes, OQS_KEM_alg_frodokem_976_aes,
                               192, 0)
MAKE_KEM_KEYMGMT_FUNCTIONS(frodo976shake, OQS_KEM_alg_frodokem_976_shake, 192)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p384_frodo976shake,
                               OQS_KEM_alg_frodokem_976_shake, 192)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x448_frodo976shake,
                               OQS_KEM_alg_frodokem_976_shake, 192, 0)
MAKE_KEM_KEYMGMT_FUNCTIONS(frodo1344aes, OQS_KEM_alg_frodokem_1344_aes, 256)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p521_frodo1344aes, OQS_KEM_alg_frodokem_1344_aes,
                               256)
MAKE_KEM_KEYMGMT_FUNCTIONS(frodo1344shake, OQS_KEM_alg_frodokem_1344_shake, 256)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p521_frodo1344shake,
                               OQS_KEM_alg_frodokem_1344_shake, 256)
MAKE_KEM_KEYMGMT_FUNCTIONS(mlkem512, OQS_KEM_alg_ml_kem_512, 128)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p256_mlkem512, OQS_KEM_alg_ml_kem_512, 128)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x25519_mlkem512, OQS_KEM_alg_ml_kem_512, 128, 1)
MAKE_KEM_KEYMGMT_FUNCTIONS(mlkem768, OQS_KEM_alg_ml_kem_768, 192)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p384_mlkem768, OQS_KEM_alg_ml_kem_768, 192)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x448_mlkem768, OQS_KEM_alg_ml_kem_768, 192, 1)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(X25519MLKEM768, OQS_KEM_alg_ml_kem_768, 128, 1)
MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(SecP256r1MLKEM768, OQS_KEM_alg_ml_kem_768, 128)
MAKE_KEM_KEYMGMT_FUNCTIONS(mlkem1024, OQS_KEM_alg_ml_kem_1024, 256)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p521_mlkem1024, OQS_KEM_alg_ml_kem_1024, 256)
MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(SecP384r1MLKEM1024, OQS_KEM_alg_ml_kem_1024, 192)
MAKE_KEM_KEYMGMT_FUNCTIONS(bikel1, OQS_KEM_alg_bike_l1, 128)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p256_bikel1, OQS_KEM_alg_bike_l1, 128)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x25519_bikel1, OQS_KEM_alg_bike_l1, 128, 0)
MAKE_KEM_KEYMGMT_FUNCTIONS(bikel3, OQS_KEM_alg_bike_l3, 192)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p384_bikel3, OQS_KEM_alg_bike_l3, 192)

MAKE_KEM_ECX_KEYMGMT_FUNCTIONS(x448_bikel3, OQS_KEM_alg_bike_l3, 192, 0)
MAKE_KEM_KEYMGMT_FUNCTIONS(bikel5, OQS_KEM_alg_bike_l5, 256)

MAKE_KEM_ECP_KEYMGMT_FUNCTIONS(p521_bikel5, OQS_KEM_alg_bike_l5, 256)
///// OQS_TEMPLATE_FRAGMENT_KEYMGMT_FUNCTIONS_END
