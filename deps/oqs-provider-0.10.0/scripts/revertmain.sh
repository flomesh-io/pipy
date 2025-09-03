#!/bin/bash

# This script reverts to a possibly set "main" code generator script

if [ -f oqs-template/generate.yml-main ]; then
    rm -rf liboqs && git clone --depth 1 --branch main https://github.com/open-quantum-safe/liboqs.git
    mv oqs-template/generate.yml-main oqs-template/generate.yml
    LIBOQS_SRC_DIR=`pwd`/liboqs python3 oqs-template/generate.py
    if [ $? -ne 0 ]; then
       echo "Code generation failure for main branch. Exiting."
       exit -1
    fi
    # remove liboqs.a to ensure rebuild against newly generated code
    rm .local/lib/liboqs.a
    git status
fi

