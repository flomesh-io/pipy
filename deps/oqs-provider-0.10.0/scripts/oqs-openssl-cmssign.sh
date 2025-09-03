#!/bin/bash

# Use dockerimage to generate CMS data for alg $1

IMAGE=openquantumsafe/curl

if [ $# -ne 1 ]; then
    echo "Usage: $0 <algorithmname>. Exiting."
    exit 1
fi

# Assumes certgen has been run before: Quick check

if [ -f tmp/$1_CA.crt ]; then
   echo "Sometext to sign" > tmp/inputfile
else
   echo "File tmp/$1_CA.crt not found. Did certgen run before? Exiting."
   exit -1
fi

openssl_version=$($OPENSSL_APP version)

if [[ "$openssl_version" == "OpenSSL 3.0."* ]]; then
        echo "Skipping CMS test for OpenSSL 3.0"
        exit 0
fi

if [[ -z "$CIRCLECI" ]]; then
docker run -v `pwd`/tmp:/home/oqs/data -it $IMAGE sh -c "cd /home/oqs/data && openssl cms -in inputfile -sign -signer $1_srv.crt -inkey $1_srv.key -nodetach -outform pem -binary -out signedfile.cms && openssl cms -verify -CAfile $1_CA.crt -inform pem -in signedfile.cms -crlfeol -out signeddatafile "
else
# CCI doesn't permit mounting, so let's do as per https://circleci.com/docs/2.0/building-docker-images/#mounting-folders:
docker create -v /certs --name certs alpine /bin/true && \
touch tmp/signedfile.cms && touch tmp/signeddatafile && chmod gou+rw tmp/* && \
docker cp tmp/ certs:/certs && \
docker run --volumes-from certs --rm --name oqsossl -it $IMAGE sh -c "cd /certs/tmp && openssl cms -in inputfile -sign -signer $1_srv.crt -inkey $1_srv.key -nodetach -outform pem -binary -out signedfile.cms && openssl cms -verify -CAfile $1_CA.crt -inform pem -in signedfile.cms -crlfeol -out signeddatafile " && \
docker cp certs:/certs/tmp . && \
docker rm certs
fi

