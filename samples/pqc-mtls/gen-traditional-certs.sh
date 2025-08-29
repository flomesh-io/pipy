#!/bin/bash

# Generate certificates for PQC testing based on OpenSSL version
# This script adapts certificate generation based on OpenSSL capabilities:
# - OpenSSL >= 3.5: Traditional certificates + PQC key exchange only  
# - OpenSSL 3.2-3.4: Can use PQC signatures if oqs-provider is available
# - OpenSSL < 3.2: Not supported for PQC

set -e

# Detect OpenSSL version
OPENSSL_VERSION=$(openssl version | cut -d' ' -f2)
echo "Detected OpenSSL version: $OPENSSL_VERSION"

# Parse version numbers for comparison
version_compare() {
    echo "$@" | awk -F. '{ printf("%d%03d%03d\n", $1,$2,$3); }'
}

CURRENT_VERSION=$(version_compare $OPENSSL_VERSION)
VERSION_3_2_0=$(version_compare "3.2.0")
VERSION_3_5_0=$(version_compare "3.5.0")

if [ "$CURRENT_VERSION" -lt "$VERSION_3_2_0" ]; then
    echo "Error: OpenSSL version $OPENSSL_VERSION is not supported for PQC testing."
    echo "Please upgrade to OpenSSL 3.2.0 or later."
    exit 1
fi

echo "Generating certificates for PQC testing..."

# Create certificates directory
mkdir -p certs
cd certs

if [ "$CURRENT_VERSION" -ge "$VERSION_3_5_0" ]; then
    echo ""
    echo "=== OpenSSL >= 3.5 Configuration ==="
    echo "Using traditional certificates with PQC key exchange support"
    echo "Note: PQC signatures are not available due to OID conflicts"
    echo ""
    
    # Use traditional algorithms for certificates
    CA_ALG="RSA"
    CA_OPTS="-pkeyopt rsa_keygen_bits:2048"
    SERVER_ALG="EC" 
    SERVER_OPTS="-pkeyopt ec_paramgen_curve:P-256"
    CLIENT_ALG="EC"
    CLIENT_OPTS="-pkeyopt ec_paramgen_curve:P-256"
    
    TEST_COMMANDS="
Available PQC key exchange tests:
  pipy server.js -- --kem=ML-KEM-512
  pipy server.js -- --kem=ML-KEM-768  
  pipy server.js -- --kem=ML-KEM-1024
  
  pipy client.js -- --kem=ML-KEM-768
  
Note: Signature algorithms will be automatically ignored in OpenSSL >= 3.5"
    
elif [ "$CURRENT_VERSION" -ge "$VERSION_3_2_0" ]; then
    echo ""
    echo "=== OpenSSL 3.2-3.4 Configuration ==="
    echo "Checking for oqs-provider availability..."
    
    # Check if oqs-provider is available
    if openssl list -providers | grep -q "oqsprovider"; then
        echo "oqs-provider detected - full PQC support available"
        echo "Using PQC algorithms for certificates and key exchange"
        
        # Use PQC algorithms for certificates
        CA_ALG="ML-DSA-65"
        CA_OPTS=""
        SERVER_ALG="ML-DSA-44"
        SERVER_OPTS=""
        CLIENT_ALG="ML-DSA-44" 
        CLIENT_OPTS=""
        
        TEST_COMMANDS="
Available PQC tests (full support):
  pipy server.js -- --kem=ML-KEM-768 --sig=ML-DSA-65
  pipy server.js -- --kem=ML-KEM-1024 --sig=SLH-DSA-128s
  pipy server.js -- --kem=ML-KEM-512 --no-hybrid
  
  pipy client.js -- --kem=ML-KEM-768 --sig=ML-DSA-65"
    else
        echo "oqs-provider not available - using traditional certificates"
        echo "Only PQC key exchange will be available (if built-in support exists)"
        
        # Use traditional algorithms
        CA_ALG="RSA"
        CA_OPTS="-pkeyopt rsa_keygen_bits:2048"
        SERVER_ALG="EC"
        SERVER_OPTS="-pkeyopt ec_paramgen_curve:P-256"
        CLIENT_ALG="EC"
        CLIENT_OPTS="-pkeyopt ec_paramgen_curve:P-256"
        
        TEST_COMMANDS="
Available tests (limited PQC support):
  pipy server.js -- --kem=ML-KEM-768
  pipy client.js -- --kem=ML-KEM-768
  
Note: PQC signatures require oqs-provider"
    fi
fi

# Generate CA private key
echo "1. Generating CA private key using $CA_ALG..."
if [ "$CA_ALG" = "RSA" ]; then
    openssl genpkey -algorithm RSA -out ca-key.pem -pkeyopt rsa_keygen_bits:2048
elif [ "$CA_ALG" = "EC" ]; then
    openssl genpkey -algorithm EC -out ca-key.pem -pkeyopt ec_paramgen_curve:P-256
else
    # PQC algorithm
    openssl genpkey -algorithm $CA_ALG -out ca-key.pem $CA_OPTS
fi

# Generate CA certificate
echo "2. Generating CA certificate..."
openssl req -new -x509 -key ca-key.pem -days 365 -out ca-cert.pem \
    -subj "/C=US/ST=CA/L=San Francisco/O=PQC Test CA/CN=PQC Test Root CA"

# Generate server private key
echo "3. Generating server private key using $SERVER_ALG..."
if [ "$SERVER_ALG" = "RSA" ]; then
    openssl genpkey -algorithm RSA -out server-key.pem -pkeyopt rsa_keygen_bits:2048
elif [ "$SERVER_ALG" = "EC" ]; then
    openssl genpkey -algorithm EC -out server-key.pem -pkeyopt ec_paramgen_curve:P-256
else
    # PQC algorithm
    openssl genpkey -algorithm $SERVER_ALG -out server-key.pem $SERVER_OPTS
fi

# Generate server certificate signing request
echo "4. Generating server CSR..."
openssl req -new -key server-key.pem -out server.csr \
    -subj "/C=US/ST=CA/L=San Francisco/O=PQC Test Server/CN=localhost"

# Generate server certificate
echo "5. Generating server certificate..."
openssl x509 -req -in server.csr -CA ca-cert.pem -CAkey ca-key.pem \
    -CAcreateserial -days 365 -out server-cert.pem

# Generate client private key
echo "6. Generating client private key using $CLIENT_ALG..."
if [ "$CLIENT_ALG" = "RSA" ]; then
    openssl genpkey -algorithm RSA -out client-key.pem -pkeyopt rsa_keygen_bits:2048
elif [ "$CLIENT_ALG" = "EC" ]; then
    openssl genpkey -algorithm EC -out client-key.pem -pkeyopt ec_paramgen_curve:P-256
else
    # PQC algorithm
    openssl genpkey -algorithm $CLIENT_ALG -out client-key.pem $CLIENT_OPTS
fi

# Generate client certificate signing request
echo "7. Generating client CSR..."
openssl req -new -key client-key.pem -out client.csr \
    -subj "/C=US/ST=CA/L=San Francisco/O=PQC Test Client/CN=pipy-client"

# Generate client certificate
echo "8. Generating client certificate..."
openssl x509 -req -in client.csr -CA ca-cert.pem -CAkey ca-key.pem \
    -CAcreateserial -days 365 -out client-cert.pem

# Clean up CSR files
rm -f server.csr client.csr ca-cert.srl

echo ""
echo "Certificates generated successfully!"
echo "Certificate files:"
echo "  - ca-cert.pem      (Root CA certificate using $CA_ALG)"
echo "  - server-cert.pem  (Server certificate using $SERVER_ALG)"  
echo "  - server-key.pem   (Server private key)"
echo "  - client-cert.pem  (Client certificate using $CLIENT_ALG)"
echo "  - client-key.pem   (Client private key)"
echo ""
echo "$TEST_COMMANDS"