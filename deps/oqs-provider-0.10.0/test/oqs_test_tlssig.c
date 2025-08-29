// SPDX-License-Identifier: Apache-2.0 AND MIT

#include <errno.h>
#include <openssl/core_names.h>
#include <openssl/provider.h>
#include <openssl/ssl.h>
#include <openssl/trace.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "test_common.h"
#include "tlstest_helpers.h"

static OSSL_LIB_CTX *libctx = NULL;
static char *modulename = NULL;
static char *configfile = NULL;
static char *certsdir = NULL;

#ifdef OSSL_CAPABILITY_TLS_SIGALG_NAME
static int test_oqs_tlssig(const char *sig_name, int dtls_flag) {
    SSL_CTX *cctx = NULL, *sctx = NULL;
    SSL *clientssl = NULL, *serverssl = NULL;
    int ret = 1, testresult = 0;
    char certpath[300];
    char privkeypath[300];
#ifndef OPENSSL_SYS_VMS
    const char *sep = "/";
#else
    const char *sep = "";
#endif

    if (!alg_is_enabled(sig_name)) {
        printf("Not testing disabled algorithm %s.\n", sig_name);
        return 1;
    }

    sprintf(certpath, "%s%s%s%s", certsdir, sep, sig_name, "_srv.crt");
    sprintf(privkeypath, "%s%s%s%s", certsdir, sep, sig_name, "_srv.key");
    /* ensure certsdir exists */
    if (mkdir(certsdir, 0700)) {
        if (errno != EEXIST) {
            fprintf(stderr, "Couldn't create certsdir %s: Err = %d\n", certsdir,
                    errno);
            ret = -1;
            goto err;
        }
    }
    if (!create_cert_key(libctx, (char *)sig_name, certpath, privkeypath)) {
        fprintf(stderr, "Cert/keygen failed for %s at %s/%s\n", sig_name,
                certpath, privkeypath);
        ret = -1;
        goto err;
    }

    testresult = create_tls1_3_ctx_pair(libctx, &sctx, &cctx, certpath,
                                        privkeypath, dtls_flag);

    if (!testresult) {
        ret = -1;
        goto err;
    }

    testresult =
        create_tls_objects(sctx, cctx, &serverssl, &clientssl, dtls_flag);

    if (!testresult) {
        ret = -2;
        goto err;
    }

    testresult = create_tls_connection(serverssl, clientssl, SSL_ERROR_NONE);
    if (!testresult) {
        ret = -5;
        goto err;
    }

err:
    SSL_free(serverssl);
    SSL_free(clientssl);
    SSL_CTX_free(sctx);
    SSL_CTX_free(cctx);
    return ret;
}

/* reactivate when EVP_SIGNATURE_do_all_provided doesn't crash any more:
static void test_oqs_sigs(EVP_SIGNATURE *evpsig, void *vp) {
        OSSL_PROVIDER* prov = EVP_SIGNATURE_get0_provider(evpsig);
        if (!strcmp(OSSL_PROVIDER_get0_name(prov), "oqsprovider")) {
                printf("Commencing test of %s:\n",
EVP_SIGNATURE_get0_name(evpsig));
                test_oqs_tlssig(EVP_SIGNATURE_get0_name(evpsig));
        }
}
*/

static int test_signature(const OSSL_PARAM params[], void *data) {
    int ret = 0;
    int *errcnt = (int *)data;
    const OSSL_PARAM *p =
        OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_SIGALG_NAME);

    if (p == NULL || p->data_type != OSSL_PARAM_UTF8_STRING) {
        ret = -1;
        goto err;
    }

    char *sigalg_name = OPENSSL_strdup(p->data);

    if (sigalg_name == NULL)
        return 0;

    ret = test_oqs_tlssig(sigalg_name, 0);

    if (ret >= 0) {
        fprintf(stderr,
                cGREEN "  TLS-SIG handshake test succeeded: %s" cNORM "\n",
                sigalg_name);
    } else {
        fprintf(stderr,
                cRED
                "  TLS-SIG handshake test failed: %s, return code: %d" cNORM
                "\n",
                sigalg_name, ret);
        ERR_print_errors_fp(stderr);
        (*errcnt)++;
    }

#ifdef DTLS1_3_VERSION
    ret = test_oqs_tlssig(sigalg_name, 1);

    if (ret >= 0) {
        fprintf(stderr,
                cGREEN "  DTLS-SIG handshake test succeeded: %s" cNORM "\n",
                sigalg_name);
    } else {
        fprintf(stderr,
                cRED
                "  DTLS-SIG handshake test failed: %s, return code: %d" cNORM
                "\n",
                sigalg_name, ret);
        ERR_print_errors_fp(stderr);
        (*errcnt)++;
    }
#endif

err:
    OPENSSL_free(sigalg_name);
    return ret;
}

static int test_provider_signatures(OSSL_PROVIDER *provider, void *vctx) {
    const char *provname = OSSL_PROVIDER_get0_name(provider);

    if (!strcmp(provname, PROVIDER_NAME_OQS))
        return OSSL_PROVIDER_get_capabilities(provider, "TLS-SIGALG",
                                              test_signature, vctx);
    else
        return 1;
}
#endif /* OSSL_CAPABILITY_TLS_SIGALG_NAME */

int main(int argc, char *argv[]) {
    size_t i;
    int errcnt = 0, test = 0;

#ifndef OPENSSL_NO_TRACE
    fprintf(stderr,
            "Full tracing enabled via openssl config 'enable-trace'.\n");
    BIO *err = BIO_new_fp(stderr, BIO_NOCLOSE | BIO_FP_TEXT);
    OSSL_trace_set_channel(OSSL_TRACE_CATEGORY_ALL, err);
#endif

    T((libctx = OSSL_LIB_CTX_new()) != NULL);
    T(argc == 4);
    modulename = argv[1];
    configfile = argv[2];
    certsdir = argv[3];

    load_oqs_provider(libctx, modulename, configfile);

    T(OSSL_PROVIDER_available(libctx, "default"));

#ifdef OSSL_CAPABILITY_TLS_SIGALG_NAME
    // crashes: EVP_SIGNATURE_do_all_provided(libctx, test_oqs_sigs, &errcnt);
    OSSL_PROVIDER_do_all(libctx, test_provider_signatures, &errcnt);
#else
    fprintf(stderr,
            "TLS-SIG handshake test not enabled. Update OpenSSL to more "
            "current version.\n");
#endif

    OSSL_LIB_CTX_free(libctx);
    TEST_ASSERT(errcnt == 0)
    return !test;
}
