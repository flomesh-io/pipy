#!/bin/bash

# Use newly built oqsprovider to generate certs for alg $1

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

echo "oqsprovider-certverify.sh commencing..."

# check that CSR can be output OK

$OPENSSL_APP req -text -in tmp/$1_srv.csr -noout 2>&1 | grep Error
if [ $? -eq 0 ]; then
    echo "Couldn't print CSR correctly. Exiting."
    exit 1
fi

$OPENSSL_APP verify -CAfile tmp/$1_CA.crt tmp/$1_srv.crt 

