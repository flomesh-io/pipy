#!/bin/bash

set -e 

# Use newly built oqsprovider to test interop with external sites

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

# We assume the value of env var HTTP_PROXY is "http://host.domain:port_num"
if [ ! -z "${HTTP_PROXY}" ]; then
    echo "Using Web proxy \"${HTTP_PROXY}\""
    export USE_PROXY="-proxy ${HTTP_PROXY#http://} -allow_proxy_certs"
else
    export USE_PROXY=""
fi

# Ascertain algorithms are available:

# Cloudflare seems to have disabled this algorithm: Remove for good?
# echo " Cloudflare:"
# 
# if ! ($OPENSSL_APP list -kem-algorithms | grep x25519_kyber768); then
#    echo "Skipping unconfigured x25519_kyber768 interop test"
# else
#    (echo -e "GET /cdn-cgi/trace HTTP/1.1\nHost: cloudflare.com\n\n"; sleep 1; echo $'\cc') | "${OPENSSL_APP}" s_client ${USE_PROXY} -connect pq.cloudflareresearch.com:443 -groups x25519_kyber768 -servername cloudflare.com -ign_eof 2>/dev/null | grep kex=X25519Kyber768Draft00
# fi

echo " Google:"

if ! ($OPENSSL_APP list -kem-algorithms | grep x25519_kyber768); then
   echo "Skipping unconfigured x25519_kyber768 interop test"
else
   echo -e "GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n" | "${OPENSSL_APP}" s_client ${USE_PROXY} -connect google.com:443 -groups x25519_kyber768 -servername google.com >/dev/null 2>/dev/null 
fi


if ! ($OPENSSL_APP list -kem-algorithms | grep X25519MLKEM768); then
   echo "Skipping unconfigured X25519MLKEM768 interop test"
else
   echo -e "GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n" | "${OPENSSL_APP}" s_client ${USE_PROXY} -connect google.com:443 -groups X25519MLKEM768 -servername google.com >/dev/null 2>/dev/null 
fi
