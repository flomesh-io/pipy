// SPDX-License-Identifier: Apache-2.0 AND MIT
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "test_common.h"

#define MAXLOOPS 1000000

/* Stolen from openssl/tests/sslapitest.c: */
int create_cert_key(OSSL_LIB_CTX *libctx, char *algname, char *certfilename,
                    char *privkeyfilename) {
    EVP_PKEY_CTX *evpctx =
        EVP_PKEY_CTX_new_from_name(libctx, algname, OQSPROV_PROPQ);
    EVP_PKEY *pkey = NULL;
    X509 *x509 = X509_new();
    X509_NAME *name = NULL;
    BIO *keybio = NULL, *certbio = NULL;
    int ret = 1;

    if (!evpctx || !EVP_PKEY_keygen_init(evpctx) ||
        !EVP_PKEY_generate(evpctx, &pkey) || !pkey || !x509 ||
        !ASN1_INTEGER_set(X509_get_serialNumber(x509), 1) ||
        !X509_gmtime_adj(X509_getm_notBefore(x509), 0) ||
        !X509_gmtime_adj(X509_getm_notAfter(x509), 31536000L) ||
        !X509_set_pubkey(x509, pkey) || !(name = X509_get_subject_name(x509)) ||
        !X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                                    (unsigned char *)"CH", -1, -1, 0) ||
        !X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                    (unsigned char *)"test.org", -1, -1, 0) ||
        !X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                    (unsigned char *)"localhost", -1, -1, 0) ||
        !X509_set_issuer_name(x509, name) ||
        !X509_sign(x509, pkey, EVP_sha256()) ||
        !(keybio = BIO_new_file(privkeyfilename, "wb")) ||
        !PEM_write_bio_PrivateKey(keybio, pkey, NULL, NULL, 0, NULL, NULL) ||
        !(certbio = BIO_new_file(certfilename, "wb")) ||
        !PEM_write_bio_X509(certbio, x509))
        ret = 0;

    EVP_PKEY_free(pkey);
    X509_free(x509);
    EVP_PKEY_CTX_free(evpctx);
    BIO_free(keybio);
    BIO_free(certbio);
    return ret;
}
/* end steal */
int create_tls1_3_ctx_pair(OSSL_LIB_CTX *libctx, SSL_CTX **sctx, SSL_CTX **cctx,
                           char *certfile, char *privkeyfile, int dtls_flag) {
    SSL_CTX *serverctx = NULL, *clientctx = NULL;

    if (sctx == NULL || cctx == NULL)
        goto err;

    if (dtls_flag) {
        serverctx = SSL_CTX_new_ex(libctx, NULL, DTLS_server_method());
        clientctx = SSL_CTX_new_ex(libctx, NULL, DTLS_client_method());
    } else {
        serverctx = SSL_CTX_new_ex(libctx, NULL, TLS_server_method());
        clientctx = SSL_CTX_new_ex(libctx, NULL, TLS_client_method());
    }

    if (serverctx == NULL || clientctx == NULL)
        goto err;

    SSL_CTX_set_options(serverctx, SSL_OP_ALLOW_CLIENT_RENEGOTIATION);
    if (dtls_flag) {
#ifdef DTLS1_3_VERSION
        if (!SSL_CTX_set_min_proto_version(serverctx, DTLS1_3_VERSION) ||
            !SSL_CTX_set_max_proto_version(serverctx, DTLS1_3_VERSION) ||
            !SSL_CTX_set_min_proto_version(clientctx, DTLS1_3_VERSION) ||
            !SSL_CTX_set_max_proto_version(clientctx, DTLS1_3_VERSION))
#endif
            goto err;
    } else {
        if (!SSL_CTX_set_min_proto_version(serverctx, TLS1_3_VERSION) ||
            !SSL_CTX_set_max_proto_version(serverctx, TLS1_3_VERSION) ||
            !SSL_CTX_set_min_proto_version(clientctx, TLS1_3_VERSION) ||
            !SSL_CTX_set_max_proto_version(clientctx, TLS1_3_VERSION))
            goto err;
    }

    if (!SSL_CTX_use_certificate_file(serverctx, certfile, SSL_FILETYPE_PEM))
        goto err;

    if (!SSL_CTX_use_PrivateKey_file(serverctx, privkeyfile, SSL_FILETYPE_PEM))
        goto err;

    if (!SSL_CTX_check_private_key(serverctx))
        goto err;

    *sctx = serverctx;
    *cctx = clientctx;
    return 1;

err:
    SSL_CTX_free(serverctx);
    SSL_CTX_free(clientctx);
    return 0;
}

int create_tls_objects(SSL_CTX *serverctx, SSL_CTX *clientctx, SSL **sssl,
                       SSL **cssl, int use_dgram) {
    SSL *serverssl = NULL, *clientssl = NULL;
    BIO *s_to_c_bio = NULL, *c_to_s_bio = NULL;

    if (serverctx == NULL || clientctx == NULL)
        goto err;

    serverssl = SSL_new(serverctx);
    clientssl = SSL_new(clientctx);

    if (serverssl == NULL || clientssl == NULL)
        goto err;

    if (use_dgram) {
#if (OPENSSL_VERSION_PREREQ(3, 2))
        s_to_c_bio = BIO_new(BIO_s_dgram_mem());
        c_to_s_bio = BIO_new(BIO_s_dgram_mem());
#else
        fprintf(stderr, "No DGRAM memory supported in this OpenSSL version.\n");
        ERR_print_errors_fp(stderr);
        goto err;
#endif
    } else {
        s_to_c_bio = BIO_new(BIO_s_mem());
        c_to_s_bio = BIO_new(BIO_s_mem());
    }

    if (s_to_c_bio == NULL || c_to_s_bio == NULL)
        goto err;

    /* Set Non-blocking IO behaviour */
    BIO_set_mem_eof_return(s_to_c_bio, -1);
    BIO_set_mem_eof_return(c_to_s_bio, -1);

    /* Up ref these as we are passing them to two SSL objects */
    SSL_set_bio(serverssl, c_to_s_bio, s_to_c_bio);
    BIO_up_ref(s_to_c_bio);
    BIO_up_ref(c_to_s_bio);
    SSL_set_bio(clientssl, s_to_c_bio, c_to_s_bio);

    *sssl = serverssl;
    *cssl = clientssl;

    return 1;

err:
    SSL_free(serverssl);
    SSL_free(clientssl);
    BIO_free(s_to_c_bio);
    BIO_free(c_to_s_bio);

    return 0;
}

/* Create an SSL connection, but does not read any post-handshake
 * NewSessionTicket messages.
 * We stop the connection attempt (and return a failure value) if either peer
 * has SSL_get_error() return the value in the |want| parameter. The connection
 * attempt could be restarted by a subsequent call to this function.
 */
int create_bare_tls_connection(SSL *serverssl, SSL *clientssl, int want,
                               int read) {
    int retc = -1, rets = -1, err, abortctr = 0;
    int clienterr = 0, servererr = 0;

    do {
        err = SSL_ERROR_WANT_WRITE;
        while (!clienterr && retc <= 0 && err == SSL_ERROR_WANT_WRITE) {
            retc = SSL_connect(clientssl);
            if (retc <= 0)
                err = SSL_get_error(clientssl, retc);
        }

        if (!clienterr && retc <= 0 && err != SSL_ERROR_WANT_READ) {
            fprintf(stderr,
                    "SSL_connect() failed returning %d, SSL error %d.\n", retc,
                    err);
            ERR_print_errors_fp(stderr);
            if (want != SSL_ERROR_SSL)
                ERR_clear_error();
            clienterr = 1;
        }
        if (want != SSL_ERROR_NONE && err == want)
            return 0;

        err = SSL_ERROR_WANT_WRITE;
        while (!servererr && rets <= 0 && err == SSL_ERROR_WANT_WRITE) {
            rets = SSL_accept(serverssl);
            if (rets <= 0)
                err = SSL_get_error(serverssl, rets);
        }

        if (!servererr && rets <= 0 && err != SSL_ERROR_WANT_READ &&
            err != SSL_ERROR_WANT_X509_LOOKUP) {
            fprintf(stderr, "SSL_accept() failed returning %d, SSL error %d.\n",
                    rets, err);
            ERR_print_errors_fp(stderr);
            if (want != SSL_ERROR_SSL)
                ERR_clear_error();
            servererr = 1;
        }
        if (want != SSL_ERROR_NONE && err == want)
            return 0;
        if (clienterr && servererr)
            return 0;

        if (++abortctr == MAXLOOPS) {
            fprintf(stderr, "No progress made");
            return 0;
        }
    } while (retc <= 0 || rets <= 0);

    return 1;
}

/*
 * Create an SSL connection including any post handshake NewSessionTicket
 * messages.
 */
int create_tls_connection(SSL *serverssl, SSL *clientssl, int want) {
    int i;
    unsigned char buf;
    size_t readbytes;

    if (!create_bare_tls_connection(serverssl, clientssl, want, 1))
        return 0;

    /*
     * We attempt to read some data on the client side which we expect to fail.
     * This will ensure we have received the NewSessionTicket in TLSv1.3 where
     * appropriate. We do this twice because there are 2 NewSessionTickets.
     */
    for (i = 0; i < 2; i++) {
        if (SSL_read_ex(clientssl, &buf, sizeof(buf), &readbytes) > 0) {
            if (readbytes != 0)
                return 0;
        } else if (SSL_get_error(clientssl, 0) != SSL_ERROR_WANT_READ) {
            return 0;
        }
    }

    return 1;
}
