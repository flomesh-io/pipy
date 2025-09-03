// SPDX-License-Identifier: Apache-2.0 AND MIT

/*
 * OQS OpenSSL 3 provider
 *
 * Code strongly inspired by OpenSSL endecoder.
 *
 * ToDo: Adding hybrid alg support
 */

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/core.h>

#include "oqs_endecoder_local.h"

OSSL_FUNC_keymgmt_new_fn *oqs_prov_get_keymgmt_new(const OSSL_DISPATCH *fns) {
    /* Pilfer the keymgmt dispatch table */
    for (; fns->function_id != 0; fns++)
        if (fns->function_id == OSSL_FUNC_KEYMGMT_NEW)
            return OSSL_FUNC_keymgmt_new(fns);

    return NULL;
}

OSSL_FUNC_keymgmt_free_fn *oqs_prov_get_keymgmt_free(const OSSL_DISPATCH *fns) {
    /* Pilfer the keymgmt dispatch table */
    for (; fns->function_id != 0; fns++)
        if (fns->function_id == OSSL_FUNC_KEYMGMT_FREE)
            return OSSL_FUNC_keymgmt_free(fns);

    return NULL;
}

OSSL_FUNC_keymgmt_import_fn *
oqs_prov_get_keymgmt_import(const OSSL_DISPATCH *fns) {
    /* Pilfer the keymgmt dispatch table */
    for (; fns->function_id != 0; fns++)
        if (fns->function_id == OSSL_FUNC_KEYMGMT_IMPORT)
            return OSSL_FUNC_keymgmt_import(fns);

    return NULL;
}

OSSL_FUNC_keymgmt_export_fn *
oqs_prov_get_keymgmt_export(const OSSL_DISPATCH *fns) {
    /* Pilfer the keymgmt dispatch table */
    for (; fns->function_id != 0; fns++)
        if (fns->function_id == OSSL_FUNC_KEYMGMT_EXPORT)
            return OSSL_FUNC_keymgmt_export(fns);

    return NULL;
}

void *oqs_prov_import_key(const OSSL_DISPATCH *fns, void *provctx,
                          int selection, const OSSL_PARAM params[]) {
    OSSL_FUNC_keymgmt_new_fn *kmgmt_new = oqs_prov_get_keymgmt_new(fns);
    OSSL_FUNC_keymgmt_free_fn *kmgmt_free = oqs_prov_get_keymgmt_free(fns);
    OSSL_FUNC_keymgmt_import_fn *kmgmt_import =
        oqs_prov_get_keymgmt_import(fns);
    void *key = NULL;

    if (kmgmt_new != NULL && kmgmt_import != NULL && kmgmt_free != NULL) {
        if ((key = kmgmt_new(provctx)) == NULL ||
            !kmgmt_import(key, selection, params)) {
            kmgmt_free(key);
            key = NULL;
        }
    }
    return key;
}

void oqs_prov_free_key(const OSSL_DISPATCH *fns, void *key) {
    OSSL_FUNC_keymgmt_free_fn *kmgmt_free = oqs_prov_get_keymgmt_free(fns);

    if (kmgmt_free != NULL)
        kmgmt_free(key);
}
