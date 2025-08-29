#!/bin/bash

# Use newly built oqsprovider to generate CMS signed files for alg $1
# Assumed oqsprovider-certgen.sh to have run before for same algorithm

# uncomment to see what's happening:
# set -x

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

openssl_version=$($OPENSSL_APP version)

if [[ "$openssl_version" == "OpenSSL 3.0."* ]]; then
        echo "Skipping CMS test for OpenSSL 3.0"
        exit 0
fi

# Assumes certgen has been run before: Quick check for CMS file:

if [ -f tmp/signedfile.cms ]; then
    $OPENSSL_APP cms -verify -CAfile tmp/$1_CA.crt -inform pem -in tmp/signedfile.cms -crlfeol -out tmp/signeddatafile 
    diff tmp/signeddatafile tmp/inputfile
else
   echo "File tmp/signedfile.cms not found. Did CMS sign run before? Exiting."
   exit -1
fi

