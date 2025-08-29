// SPDX-License-Identifier: Apache-2.0 AND MIT

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oqs_prov.h"
#include "test_common.h"

/** \brief A pair of keys. */
struct KeyPair {
    /** \brief The public key. */
    uint8_t *pubkey;

    /** \brief The public key length, in bytes. */
    size_t pubkey_len;

    /** \brief The private key. */
    uint8_t *privkey;

    /** \brief The private key length, in bytes. */
    size_t privkey_len;

    /** \brief Indicates if the pair of keys is from a quantum-resistant
     * algorithm (1) or not (0). */
    int is_pq;
};

/** \brief Frees the memory occupied by a KeyPair.
 *
 * \param kp Keypair to free. */
static void keypair_free(struct KeyPair *kp) {
    free(kp->pubkey);
    free(kp->privkey);
}

/** \brief Initializes an OpenSSL top-level context.
 *
 * \returns The top-level context, or `NULL` if an error occurred. */
static OSSL_LIB_CTX *init_openssl(void) {
    OSSL_LIB_CTX *ctx;

    if (!(ctx = OSSL_LIB_CTX_new())) {
        fputs(cRED "failed to initialize a new `OSSL_LIB_CTX` " cNORM "\n",
              stderr);
    }

    return ctx;
}

/** \brief Initializes a context for the EVP_PKEY API.
 *
 * \param libctx Top-level OpenSSL context.
 * \paran alg The algorithm to use.
 *
 * \returns The EVP_PKEY context, or `NULL` if an error occurred. */
static EVP_PKEY_CTX *init_EVP_PKEY_CTX(OSSL_LIB_CTX *libctx, const char *alg) {
    EVP_PKEY_CTX *ctx;

    if (!(ctx = EVP_PKEY_CTX_new_from_name(libctx, alg, OQSPROV_PROPQ))) {
        fprintf(stderr,
                cRED "`EVP_PKEY_CTX_new_from_name` failed with algorithm %s: ",
                alg);
        ERR_print_errors_fp(stderr);
        fputs(cNORM "\n", stderr);
    }

    return ctx;
}

/** \brief Initializes the keygen operation on an EVP_PKEY context.
 *
 * \param ctx EVP_PKEY context.
 *
 * \returns 0 on success. */
static int init_keygen(EVP_PKEY_CTX *ctx) {
    int err;

    if ((err = EVP_PKEY_keygen_init(ctx)) == -2) {
        fputs(cRED "`EVP_PKEY_keygen_init` failed, couldn't initialize "
                   "keygen: not "
                   "supported" cNORM "\n",
              stderr);
    } else if (err <= 0) {
        fputs(cRED
              "`EVP_PKEY_keygen_init` failed, couldn't initialize keygen: ",
              stderr);
        ERR_print_errors_fp(stderr);
        fputs(cNORM "\n", stderr);
    }

    return err;
}

/** \brief Generates the private key.
 *
 * \param ctx EVP_PKEY context.
 *
 * \returns The private key, or `NULL` if an error occurred. */
static EVP_PKEY *generate_private_key(EVP_PKEY_CTX *ctx) {
    EVP_PKEY *private_key = NULL;
    int err;

    if ((err = EVP_PKEY_generate(ctx, &private_key)) == -2) {
        fputs(cRED "`EVP_PKEY_generate` failed, couldn't generate: not "
                   "supported" cNORM "\n",
              stderr);
    } else if (err <= 0) {
        fputs(cRED "`EVP_PKEY_generate` failed, couldn't generate: ", stderr);
        ERR_print_errors_fp(stderr);
        fputs(cNORM "\n", stderr);
    }

    return private_key;
}

/** \brief Extracts the classical keys from an hybrid key.
 *
 * \param private_key The private key.
 * \param[out] out Key pair where to write the keys.
 *
 * \returns 0 on success. */
static int private_key_params_get_classical_keys(const EVP_PKEY *private_key,
                                                 struct KeyPair *out) {
    int ret = -1;

    if (get_param_octet_string(private_key,
                               OQS_HYBRID_PKEY_PARAM_CLASSICAL_PUB_KEY,
                               &out->pubkey, &out->pubkey_len)) {
        goto out;
    }
    if (get_param_octet_string(private_key,
                               OQS_HYBRID_PKEY_PARAM_CLASSICAL_PRIV_KEY,
                               &out->privkey, &out->privkey_len)) {
        goto free_pubkey;
    }
    ret = 0;
    goto out;

free_pubkey:
    free(out->pubkey);

out:
    return ret;
}

/** \brief Extracts the quantum-resistant keys from an hybrid key.
 *
 * \param private_key The private key.
 * \param[out] out Key pair where to write the keys.
 *
 * \returns 0 on success. */
static int private_key_params_get_pq_keys(const EVP_PKEY *private_key,
                                          struct KeyPair *out) {
    int ret = -1;

    if (get_param_octet_string(private_key, OQS_HYBRID_PKEY_PARAM_PQ_PUB_KEY,
                               &out->pubkey, &out->pubkey_len)) {
        goto out;
    }
    if (get_param_octet_string(private_key, OQS_HYBRID_PKEY_PARAM_PQ_PRIV_KEY,
                               &out->privkey, &out->privkey_len)) {
        goto free_pubkey;
    }
    ret = 0;
    goto out;

free_pubkey:
    free(out->pubkey);

out:
    return ret;
}

/** \brief Extracts the combination of classical+hybrid keys from an hybrid
 * key.
 *
 * \param private_key The private key.
 * \param[out] out Key pair where to write the keys.
 *
 * \returns 0 on success. */
static int private_key_params_get_full_keys(const EVP_PKEY *private_key,
                                            struct KeyPair *out) {
    int ret = -1;

    if (get_param_octet_string(private_key, OSSL_PKEY_PARAM_PUB_KEY,
                               &out->pubkey, &out->pubkey_len)) {
        goto out;
    }
    if (get_param_octet_string(private_key, OSSL_PKEY_PARAM_PRIV_KEY,
                               &out->privkey, &out->privkey_len)) {
        goto free_pubkey;
    }
    ret = 0;
    goto out;

free_pubkey:
    free(out->pubkey);

out:
    return ret;
}

/** \brief Reconstitutes the combination of a classical key and a
 * quantum-resistant key.
 *
 * \param classical Classical key.
 * \param classical_n Length in bytes of `classical`.
 * \param pq Quantum-resistant key.
 * \param pq_n Length in bytes of `pq`.
 * \param reverse Reverses the order of shares
 * \param[out] buf Out buffer.
 * \param[out] buf_n Length in bytes of `buf`.
 *
 * \returns 0 on success. */
static int reconstitute_keys(const uint8_t *classical, const size_t classical_n,
                             const uint8_t *pq, const size_t pq_n, int reverse,
                             uint8_t **buf, size_t *buf_len) {
    uint32_t header;
    int ret = -1;

    *buf_len = sizeof(uint32_t) + classical_n + pq_n;
    if (!(*buf = malloc(*buf_len))) {
        fprintf(stderr, cRED "failed to allocate %#zx byte(s)" cNORM "\n",
                *buf_len);
        goto out;
    }
    header = classical_n;
    (*buf)[0] = header >> 0x18;
    (*buf)[1] = header >> 0x10;
    (*buf)[2] = header >> 0x8;
    (*buf)[3] = header;

    if (!reverse) {
        memcpy(*buf + sizeof(header), classical, classical_n);
        memcpy(*buf + sizeof(header) + classical_n, pq, pq_n);
    } else {
        memcpy(*buf + sizeof(header), pq, pq_n);
        memcpy(*buf + sizeof(header) + pq_n, classical, classical_n);
    }
    ret = 0;

out:
    return ret;
}

/** \brief Verifies the consistency between pairs of keys.
 *
 * \param classical The classical keypair.
 * \param pq The quantum-resistant keypair.
 * \param comb The combination of both classical+quantum-resistant keypairs.
 *
 * \returns 0 on success. */
static int keypairs_verify_consistency(const struct KeyPair *classical,
                                       const struct KeyPair *pq,
                                       const struct KeyPair *comb) {
    uint8_t *reconstitution, *reconstitution_rev;
    size_t n;
    int ret = -1;

    if (reconstitute_keys(classical->pubkey, classical->pubkey_len, pq->pubkey,
                          pq->pubkey_len, 1, &reconstitution, &n)) {
        goto out;
    }
    if (reconstitute_keys(classical->pubkey, classical->pubkey_len, pq->pubkey,
                          pq->pubkey_len, 0, &reconstitution_rev, &n)) {
        goto out;
    }
    if (n != comb->pubkey_len) {
        fprintf(stderr,
                cRED "expected %#zx byte(s) for reconstitution of "
                     "pubkey, got %#zx" cNORM "\n",
                comb->pubkey_len, n);
        goto free_reconstitute;
    }
    if (memcmp(reconstitution, comb->pubkey, n) &&
        memcmp(reconstitution_rev, comb->pubkey, n)) {
        fputs(cRED "pubkey and comb->pubkey differ " cNORM "\n", stderr);
        fputs(cRED "pubkey: ", stderr);
        hexdump(reconstitution, n);
        fputs("\ncomb->pubkey: ", stderr);
        hexdump(comb->pubkey, n);
        fputs(cNORM "\n", stderr);
        goto free_reconstitute;
    }
    free(reconstitution);
    free(reconstitution_rev);

    if (reconstitute_keys(classical->privkey, classical->privkey_len,
                          pq->privkey, pq->privkey_len, 0, &reconstitution,
                          &n)) {
        goto out;
    }
    if (reconstitute_keys(classical->privkey, classical->privkey_len,
                          pq->privkey, pq->privkey_len, 1, &reconstitution_rev,
                          &n)) {
        goto out;
    }
    if (n != comb->privkey_len) {
        fprintf(stderr,
                "expected %#zx byte(s) for reconstitution of privkey, "
                "got %#zx\n",
                comb->privkey_len, n);
        goto free_reconstitute;
    }
    if (memcmp(reconstitution, comb->privkey, n) &&
        memcmp(reconstitution_rev, comb->privkey, n)) {
        fputs(cRED "privkey and comb->privkey differ" cNORM "\n", stderr);
        fputs(cRED "privkey: ", stderr);
        hexdump(reconstitution, n);
        fputs("\ncomb->privkey: ", stderr);
        hexdump(comb->privkey, n);
        fputs(cNORM "\n", stderr);
        goto free_reconstitute;
    }
    ret = 0;

free_reconstitute:
    free(reconstitution);
    free(reconstitution_rev);

out:
    return ret;
}

/** \brief Tests an algorithm.
 *
 * \param libctx Top-level OpenSSL context.
 * \param algname Algorithm name.
 *
 * \returns 0 on success. */
static int test_algorithm(OSSL_LIB_CTX *libctx, const char *algname) {
    EVP_PKEY_CTX *evp_pkey_ctx;
    EVP_PKEY *private_key;
    struct KeyPair classical_keypair;
    struct KeyPair pq_keypair;
    struct KeyPair full_keypair;
    int ret = -1;

    if (!(evp_pkey_ctx = init_EVP_PKEY_CTX(libctx, algname))) {
        goto out;
    }

    if (init_keygen(evp_pkey_ctx) != 1) {
        goto free_evp_pkey_ctx;
    }

    if (!(private_key = generate_private_key(evp_pkey_ctx))) {
        goto free_evp_pkey_ctx;
    }

    if (private_key_params_get_classical_keys(private_key,
                                              &classical_keypair)) {
        goto free_private_key;
    }

    if (private_key_params_get_pq_keys(private_key, &pq_keypair)) {
        goto free_classical_keypair;
    }

    if (private_key_params_get_full_keys(private_key, &full_keypair)) {
        goto free_pq_keypair;
    }

    if (!keypairs_verify_consistency(&classical_keypair, &pq_keypair,
                                     &full_keypair)) {
        ret = 0;
    }

    keypair_free(&full_keypair);

free_pq_keypair:
    keypair_free(&pq_keypair);

free_classical_keypair:
    keypair_free(&classical_keypair);

free_private_key:
    EVP_PKEY_free(private_key);

free_evp_pkey_ctx:
    EVP_PKEY_CTX_free(evp_pkey_ctx);

out:
    return ret;
}

int main(int argc, char **argv) {
    OSSL_LIB_CTX *libctx;
    OSSL_PROVIDER *default_provider;
    OSSL_PROVIDER *oqs_provider;
    const char *modulename;
    const char *configfile;
    const OSSL_ALGORITHM *algs;
    int query_nocache;
    int errcnt;
    int ret = EXIT_FAILURE;

    if (!(libctx = init_openssl())) {
        goto end;
    }

    if (!(default_provider = load_default_provider(libctx))) {
        goto free_libctx;
    }

    T(argc == 3);
    modulename = argv[1];
    configfile = argv[2];

    load_oqs_provider(libctx, modulename, configfile);
    if (!(oqs_provider = OSSL_PROVIDER_load(libctx, modulename))) {
        fputs(cRED "  `oqs_provider` is NULL " cNORM "\n", stderr);
        goto unload_default_provider;
    }

    errcnt = 0;
    algs = OSSL_PROVIDER_query_operation(oqs_provider, OSSL_OP_SIGNATURE,
                                         &query_nocache);
    if (!algs) {
        fprintf(stderr, cRED "  No signature algorithms found" cNORM "\n");
        ERR_print_errors_fp(stderr);
        ++errcnt;
        goto next_alg;
    }

    for (; algs->algorithm_names != NULL; ++algs) {
        if (!is_signature_algorithm_hybrid(algs->algorithm_names)) {
            continue;
        }
        if (test_algorithm(libctx, algs->algorithm_names)) {
            fprintf(stderr, cRED " failed for %s " cNORM "\n",
                    algs->algorithm_names);
            ++errcnt;
        } else {
            fprintf(stderr, cGREEN "%s succeeded" cNORM "\n",
                    algs->algorithm_names);
        }
    }

next_alg:
    algs = OSSL_PROVIDER_query_operation(oqs_provider, OSSL_OP_KEM,
                                         &query_nocache);
    if (!algs) {
        fprintf(stderr, cRED "  No KEM algorithms found" cNORM "\n");
        ERR_print_errors_fp(stderr);
        ++errcnt;
        goto unload_oqs_provider;
    }
    for (; algs->algorithm_names != NULL; ++algs) {
        if (!is_kem_algorithm_hybrid(algs->algorithm_names)) {
            continue;
        }
        if (test_algorithm(libctx, algs->algorithm_names)) {
            fprintf(stderr, cRED " failed for %s " cNORM "\n",
                    algs->algorithm_names);
            ++errcnt;
        } else {
            fprintf(stderr, cGREEN "%s succeeded" cNORM "\n",
                    algs->algorithm_names);
        }
    }

    if (errcnt == 0) {
        ret = EXIT_SUCCESS;
    }

unload_oqs_provider:
    OSSL_PROVIDER_unload(oqs_provider);

unload_default_provider:
    OSSL_PROVIDER_unload(default_provider);

free_libctx:
    OSSL_LIB_CTX_free(libctx);

end:
    return ret;
}
