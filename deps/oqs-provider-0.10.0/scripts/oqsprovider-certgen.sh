#!/bin/bash

set -e 
set -x

# Use newly built oqsprovider to generate certs for alg $1
# Tests use of openssl req genpkey x509 verify pkey commands

if [ $# -ne 1 ]; then
    echo "Usage: $0 <algorithmname>. Exiting."
    exit 1
fi

if [ -z "$OPENSSL_APP" ]; then
    echo "OPENSSL_APP env var not set. Exiting."
    exit 1
fi

if [ -z "$OPENSSL_MODULES" ]; then
    echo "Warning: OPENSSL_MODULES env var not set."
fi

# Set OSX DYLD_LIBRARY_PATH if not already externally set
if [ -z "$DYLD_LIBRARY_PATH" ]; then
    export DYLD_LIBRARY_PATH=$LD_LIBRARY_PATH
fi

echo "oqsprovider-certgen.sh commencing..."

#rm -rf tmp
mkdir -p tmp

$OPENSSL_APP req -x509 -new -newkey $1 -keyout tmp/$1_CA.key -out tmp/$1_CA.crt -nodes -subj "/CN=oqstest CA" -days 365 && \
$OPENSSL_APP genpkey -algorithm $1 -out tmp/$1_srv.key && \
$OPENSSL_APP req -new -newkey $1 -keyout tmp/$1_srv.key -out tmp/$1_srv.csr -nodes -subj "/CN=oqstest server" && \
$OPENSSL_APP x509 -req -in tmp/$1_srv.csr -out tmp/$1_srv.crt -CA tmp/$1_CA.crt -CAkey tmp/$1_CA.key -CAcreateserial -days 365 && \
$OPENSSL_APP verify -CAfile tmp/$1_CA.crt tmp/$1_srv.crt
# test PEM/DER/TEXT encoder/decoder logic:
$OPENSSL_APP pkey -text -in tmp/$1_CA.key
$OPENSSL_APP pkey -in tmp/$1_CA.key -outform DER -out tmp/$1_CA.der
if command -v xxd &> /dev/null; then
xxd -i tmp/$1_CA.der
fi

#fails:
#$OPENSSL_APP verify -CAfile tmp/$1_CA.crt tmp/$1_srv.crt -provider oqsprovider -provider default

