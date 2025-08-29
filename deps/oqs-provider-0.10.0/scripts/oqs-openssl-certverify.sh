#!/bin/bash

# Use dockerimage to verify certs for alg $1

IMAGE=openquantumsafe/curl

if [ $# -ne 1 ]; then
    echo "Usage: $0 <algorithmname>. Exiting."
    exit 1
fi

if [ ! -d tmp ]; then
    echo "Test folder tmp not existing. Exiting."
    exit 1
fi

if [ ! -f tmp/$1_srv.crt ]; then
    echo "Cert to test not present. Exiting."
    exit 1
fi

if [[ -z "$CIRCLECI" ]]; then
docker run -v `pwd`/tmp:/home/oqs/data -it $IMAGE sh -c "cd /home/oqs/data && openssl verify -CAfile $1_CA.crt $1_srv.crt"
else
# CCI doesn't permit mounting, so let's do as per https://circleci.com/docs/2.0/building-docker-images/#mounting-folders:
docker create -v /certs --name certs alpine /bin/true && \
chmod gou+rw tmp/* && \
docker cp tmp/ certs:/certs && \
docker run --volumes-from certs -it $IMAGE sh -c "cd /certs/tmp && openssl verify -CAfile $1_CA.crt $1_srv.crt" && \
docker rm /certs
fi
