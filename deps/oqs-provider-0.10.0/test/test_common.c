// SPDX-License-Identifier: Apache-2.0 AND MIT

#include "test_common.h"

#include <openssl/evp.h>
#include <string.h>

///// OQS_TEMPLATE_FRAGMENT_HYBRID_SIG_ALGS_START

/** \brief List of hybrid signature algorithms. */
const char *kHybridSignatureAlgorithms[] = {
    "p256_mldsa44",
    "rsa3072_mldsa44",
    "p384_mldsa65",
    "p521_mldsa87",
    "p256_falcon512",
    "rsa3072_falcon512",
    "p256_falconpadded512",
    "rsa3072_falconpadded512",
    "p521_falcon1024",
    "p521_falconpadded1024",
    "p256_sphincssha2128fsimple",
    "rsa3072_sphincssha2128fsimple",
    "p256_sphincssha2128ssimple",
    "rsa3072_sphincssha2128ssimple",
    "p384_sphincssha2192fsimple",
    "p256_sphincsshake128fsimple",
    "rsa3072_sphincsshake128fsimple",
    "p256_mayo1",
    "p256_mayo2",
    "p384_mayo3",
    "p521_mayo5",
    "p256_OV_Is_pkc",
    "p256_OV_Ip_pkc",
    "p256_OV_Is_pkc_skc",
    "p256_OV_Ip_pkc_skc",
    "p256_snova2454",
    "p256_snova2454esk",
    "p256_snova37172",
    "p384_snova2455",
    "p521_snova2965",
    NULL,
};
///// OQS_TEMPLATE_FRAGMENT_HYBRID_SIG_ALGS_END

///// OQS_TEMPLATE_FRAGMENT_COMPOSITE_SIG_ALGS_START

/** \brief List of composite signature algorithms. */
const char *kCompositeSignatureAlgorithms[] = {

    NULL,
};
///// OQS_TEMPLATE_FRAGMENT_COMPOSITE_SIG_ALGS_END

///// OQS_TEMPLATE_FRAGMENT_HYBRID_KEM_ALGS_START

/** \brief List of hybrid KEMs. */
const char *kHybridKEMAlgorithms[] = {
    "p256_frodo640aes",     "x25519_frodo640aes", "p256_frodo640shake",
    "x25519_frodo640shake", "p384_frodo976aes",   "x448_frodo976aes",
    "p384_frodo976shake",   "x448_frodo976shake", "p521_frodo1344aes",
    "p521_frodo1344shake",  "p256_mlkem512",      "x25519_mlkem512",
    "p384_mlkem768",        "x448_mlkem768",      "X25519MLKEM768",
    "SecP256r1MLKEM768",    "p521_mlkem1024",     "SecP384r1MLKEM1024",
    "p256_bikel1",          "x25519_bikel1",      "p384_bikel3",
    "x448_bikel3",          "p521_bikel5",        NULL,
}; ///// OQS_TEMPLATE_FRAGMENT_HYBRID_KEM_ALGS_END

void hexdump(const void *ptr, size_t len) {
    const unsigned char *p = ptr;
    size_t i, j;

    for (i = 0; i < len; i += j) {
        for (j = 0; j < 16 && i + j < len; j++)
            printf("%s%02x", j ? "" : " ", p[i + j]);
    }
    printf("\n");
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
int alg_is_enabled(const char *algname) {
    char *alglist = getenv("OQS_SKIP_TESTS");
    char *comma = NULL;
    char totest[200];

    if (alglist == NULL)
        return 1;

    while ((comma = strchr(alglist, ','))) {
        memcpy(totest, alglist, MIN(200, comma - alglist));
        totest[comma - alglist] = '\0';
        if (strstr(algname, totest))
            return 0;
        alglist = comma + 1;
    }
    return strstr(algname, alglist) == NULL;
}

OSSL_PROVIDER *load_default_provider(OSSL_LIB_CTX *libctx) {
    OSSL_PROVIDER *provider;
    T((provider = OSSL_PROVIDER_load(libctx, "default")));
    return provider;
}

#ifdef OQS_PROVIDER_STATIC
#define OQS_PROVIDER_ENTRYPOINT_NAME oqs_provider_init
#else
#define OQS_PROVIDER_ENTRYPOINT_NAME OSSL_provider_init
#endif // ifdef OQS_PROVIDER_STATIC

#ifndef OQS_PROVIDER_STATIC

/* Loads the oqs-provider from a shared module (.so). */
void load_oqs_provider(OSSL_LIB_CTX *libctx, const char *modulename,
                       const char *configfile) {
    T(OSSL_LIB_CTX_load_config(libctx, configfile));
    T(OSSL_PROVIDER_available(libctx, modulename));
}

#else

extern OSSL_provider_init_fn OQS_PROVIDER_ENTRYPOINT_NAME;

/* Loads the statically linked oqs-provider. */
void load_oqs_provider(OSSL_LIB_CTX *libctx, const char *modulename,
                       const char *configfile) {
    (void)configfile;
    T(OSSL_PROVIDER_add_builtin(libctx, modulename,
                                OQS_PROVIDER_ENTRYPOINT_NAME));
    T(OSSL_PROVIDER_load(libctx, "default"));
}

#endif // ifndef OQS_PROVIDER_STATIC

/** \brief Indicates if a string is in a given list of strings.
 *
 * \param list List of strings.
 * \param s String to test.
 *
 * \return 1 if `s` is in `list`, else 0. */
static int is_string_in_list(const char **list, const char *s) {
    for (; *list != NULL && strcmp(*list, s) != 0; ++list)
        ;
    if (*list != NULL) {
        return 1;
    }
    return 0;
}

int is_signature_algorithm_hybrid(const char *_alg_) {
    return is_string_in_list(kHybridSignatureAlgorithms, _alg_);
}

int is_signature_algorithm_composite(const char *_alg_) {
    return is_string_in_list(kCompositeSignatureAlgorithms, _alg_);
}

int is_kem_algorithm_hybrid(const char *_alg_) {
    return is_string_in_list(kHybridKEMAlgorithms, _alg_);
}

int get_param_octet_string(const EVP_PKEY *key, const char *param_name,
                           uint8_t **buf, size_t *buf_len) {
    *buf = NULL;
    *buf_len = 0;
    int ret = -1;

    if (EVP_PKEY_get_octet_string_param(key, param_name, NULL, 0, buf_len) !=
        1) {
        fprintf(stderr,
                cRED
                "`EVP_PKEY_get_octet_string_param` failed with param `%s`: ",
                param_name);
        ERR_print_errors_fp(stderr);
        fputs(cNORM "\n", stderr);
        goto out;
    }
    if (!(*buf = malloc(*buf_len))) {
        fprintf(stderr, "failed to allocate %#zx byte(s)\n", *buf_len);
        goto out;
    }
    if (EVP_PKEY_get_octet_string_param(key, param_name, *buf, *buf_len,
                                        buf_len) != 1) {
        fprintf(stderr,
                cRED
                "`EVP_PKEY_get_octet_string_param` failed with param `%s`: ",
                param_name);
        ERR_print_errors_fp(stderr);
        fputs(cNORM "\n", stderr);
        free(*buf);
        *buf = NULL;
    } else {
        ret = 0;
    }

out:
    return ret;
}
