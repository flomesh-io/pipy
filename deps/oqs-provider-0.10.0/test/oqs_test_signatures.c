// SPDX-License-Identifier: Apache-2.0 AND MIT

#include <openssl/evp.h>
#include <openssl/provider.h>

#include "oqs/oqs.h"
#include "test_common.h"

static OSSL_LIB_CTX *libctx = NULL;
static char *modulename = NULL;
static char *configfile = NULL;
static char *cert = NULL;
static char *privkey = NULL;
static char *certsdir = NULL;
static char *srpvfile = NULL;
static char *tmpfilename = NULL;

// sign-and-hash must work with and without providing a digest algorithm
static int test_oqs_signatures(const char *sigalg_name) {
    EVP_MD_CTX *mdctx = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *key = NULL;
    const char msg[] = "The quick brown fox jumps over... you know what";
    unsigned char *sig;
    size_t siglen;

    int testresult = 1;

    if (!alg_is_enabled(sigalg_name)) {
        printf("Not testing disabled algorithm %s.\n", sigalg_name);
        return 1;
    }
    // test with built-in digest only if default provider is active:
    // TBD revisit when hybrids are activated: They always need default
    // provider
    if (OSSL_PROVIDER_available(libctx, "default")) {
        testresult &=
            (ctx = EVP_PKEY_CTX_new_from_name(libctx, sigalg_name,
                                              OQSPROV_PROPQ)) != NULL &&
            EVP_PKEY_keygen_init(ctx) && EVP_PKEY_generate(ctx, &key) &&
            (mdctx = EVP_MD_CTX_new()) != NULL &&
            EVP_DigestSignInit_ex(mdctx, NULL, "SHA512", libctx, NULL, key,
                                  NULL) &&
            EVP_DigestSignUpdate(mdctx, msg, sizeof(msg)) &&
            EVP_DigestSignFinal(mdctx, NULL, &siglen) &&
            (sig = OPENSSL_malloc(siglen)) != NULL &&
            EVP_DigestSignFinal(mdctx, sig, &siglen) &&
            EVP_DigestVerifyInit_ex(mdctx, NULL, "SHA512", libctx, NULL, key,
                                    NULL) &&
            EVP_DigestVerifyUpdate(mdctx, msg, sizeof(msg)) &&
            EVP_DigestVerifyFinal(mdctx, sig, siglen);
        sig[0] = ~sig[0];
        testresult &= EVP_DigestVerifyInit_ex(mdctx, NULL, "SHA512", libctx,
                                              NULL, key, NULL) &&
                      EVP_DigestVerifyUpdate(mdctx, msg, sizeof(msg)) &&
                      !EVP_DigestVerifyFinal(mdctx, sig, siglen);
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    OPENSSL_free(sig);
    mdctx = NULL;
    key = NULL;

    // this test must work also with default provider inactive:
    testresult &=
        (ctx = EVP_PKEY_CTX_new_from_name(libctx, sigalg_name,
                                          OQSPROV_PROPQ)) != NULL &&
        EVP_PKEY_keygen_init(ctx) && EVP_PKEY_generate(ctx, &key) &&
        (mdctx = EVP_MD_CTX_new()) != NULL &&
        EVP_DigestSignInit_ex(mdctx, NULL, NULL, libctx, NULL, key, NULL) &&
        EVP_DigestSignUpdate(mdctx, msg, sizeof(msg)) &&
        EVP_DigestSignFinal(mdctx, NULL, &siglen) &&
        (sig = OPENSSL_malloc(siglen)) != NULL &&
        EVP_DigestSignFinal(mdctx, sig, &siglen) &&
        EVP_DigestVerifyInit_ex(mdctx, NULL, NULL, libctx, NULL, key, NULL) &&
        EVP_DigestVerifyUpdate(mdctx, msg, sizeof(msg)) &&
        EVP_DigestVerifyFinal(mdctx, sig, siglen);
    sig[0] = ~sig[0];
    testresult &=
        EVP_DigestVerifyInit_ex(mdctx, NULL, NULL, libctx, NULL, key, NULL) &&
        EVP_DigestVerifyUpdate(mdctx, msg, sizeof(msg)) &&
        !EVP_DigestVerifyFinal(mdctx, sig, siglen);

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    OPENSSL_free(sig);
    return testresult;
}

#define nelem(a) (sizeof(a) / sizeof((a)[0]))

int main(int argc, char *argv[]) {
    size_t i;
    int errcnt = 0, test = 0, query_nocache;
    OSSL_PROVIDER *oqsprov = NULL;
    const OSSL_ALGORITHM *sigalgs;

    T((libctx = OSSL_LIB_CTX_new()) != NULL);
    T(argc == 3);
    modulename = argv[1];
    configfile = argv[2];

    load_oqs_provider(libctx, modulename, configfile);

    oqsprov = OSSL_PROVIDER_load(libctx, modulename);

    sigalgs = OSSL_PROVIDER_query_operation(oqsprov, OSSL_OP_SIGNATURE,
                                            &query_nocache);
    if (sigalgs) {
        for (; sigalgs->algorithm_names != NULL; sigalgs++) {
            if (test_oqs_signatures(sigalgs->algorithm_names)) {
                fprintf(stderr,
                        cGREEN "  Signature test succeeded: %s" cNORM "\n",
                        sigalgs->algorithm_names);
            } else {
                fprintf(stderr, cRED "  Signature test failed: %s" cNORM "\n",
                        sigalgs->algorithm_names);
                ERR_print_errors_fp(stderr);
                errcnt++;
            }
        }
    }

    OSSL_LIB_CTX_free(libctx);

    TEST_ASSERT(errcnt == 0)
    return !test;
}
