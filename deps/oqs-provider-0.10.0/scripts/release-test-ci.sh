#!/bin/bash

# Stop in case of error
set -e

# To be run as part of a release test only on Linux
# requires python, pytest, xdist; install e.g. via
# sudo apt install python3 python3-pytest python3-pytest-xdist python3-psutil

# must be run in main folder
# multicore machine recommended for fast execution

# expect (ideally latest/release-test) liboqs to be already build and present
if [ -d liboqs ]; then
   export LIBOQS_SRC_DIR=`pwd`/liboqs
else
   echo "liboqs not found. Exiting."
   exit 1
fi

if [ -d oqs-template ]; then
    # Activate all algorithms
    sed -i "s/enable\: false/enable\: true/g" oqs-template/generate.yml
    python3 oqs-template/generate.py
    ./scripts/fullbuild.sh
    ./scripts/runtests.sh -V
    if [ -f .local/bin/openssl ]; then
        OPENSSL_MODULES=`pwd`/_build/lib OPENSSL_CONF=`pwd`/scripts/openssl-ca.cnf python3 -m pytest --numprocesses=auto scripts/test_tls_full.py
    else
        echo "For full TLS PQ SIG/KEM matrix test, build (latest) openssl locally."
    fi
else
    echo "$0 must be run in main oqs-provider folder. Exiting."
    exit 1
fi

