#!/bin/bash

# Use newly built oqsprovider to generate CMS signed files for alg $1
# Also used to test X509 pubkey extract and sign/verify using openssl dgst
# Assumed oqsprovider-certgen.sh to have run before for same algorithm

# uncomment to see what's happening:
#set -x

if [ $# -lt 1 ]; then
    echo "Usage: $0 <algorithmname> [digestname]. Exiting."
    exit 1
fi

echo "oqsprovider-cmssign.sh commencing..."

if [ $# -eq 2 ]; then
	DGSTNAME=$2
else
	DGSTNAME="sha512"
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

# Assumes certgen has been run before: Quick check

if [ -f tmp/$1_CA.crt ]; then
   echo "Sometext to sign" > tmp/inputfile
   # Activate to test with zero-length inputfile:
   #cat /dev/null > tmp/inputfile
else
   echo "File tmp/$1_CA.crt not found. Did certgen run before? Exiting."
   exit -1
fi

openssl_version=$($OPENSSL_APP version)

if [[ "$openssl_version" == "OpenSSL 3.0."* ]]; then
	echo "Skipping CMS test for OpenSSL 3.0: Support only starting with 3.2"
	exit 0
fi

# dgst test:
$OPENSSL_APP x509 -in tmp/$1_srv.crt -pubkey -noout > tmp/$1_srv.pubkey && $OPENSSL_APP cms -in tmp/inputfile -sign -signer tmp/$1_srv.crt -inkey tmp/$1_srv.key -nodetach -outform pem -binary -out tmp/signedfile.cms -md $DGSTNAME && $OPENSSL_APP dgst -sign tmp/$1_srv.key -out tmp/dgstsignfile tmp/inputfile

if [ $? -eq 0 ]; then
# cms test:
   $OPENSSL_APP cms -verify -CAfile tmp/$1_CA.crt -inform pem -in tmp/signedfile.cms -crlfeol -out tmp/signeddatafile && diff tmp/signeddatafile tmp/inputfile && $OPENSSL_APP dgst -signature tmp/dgstsignfile -verify tmp/$1_srv.pubkey tmp/inputfile
else
   exit -1
fi

