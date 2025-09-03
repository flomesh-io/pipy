#!/bin/bash

# Generate Post-Quantum Cryptography certificates for mTLS demo
# This script requires OpenSSL 3.5+ with oqs-provider loaded

set -e

echo "Generating PQC certificates for mTLS demo..."

# Create certificates directory
mkdir -p certs
cd certs

# Set up environment for oqs-provider
export OPENSSL_MODULES="../../../build/oqs-provider-install/lib"

# Create CA configuration
cat > ca.conf << EOF
[req]
distinguished_name = req_distinguished_name
x509_extensions = v3_ca
prompt = no

[req_distinguished_name]
CN = PQC CA
O = Pipy PQC Demo
OU = Certificate Authority
C = US

[v3_ca]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical,CA:true
keyUsage = critical,digitalSignature,cRLSign,keyCertSign
EOF

# Create server certificate configuration
cat > server.conf << EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = localhost
O = Pipy PQC Demo
OU = Server Certificate
C = US

[v3_req]
subjectAltName = @alt_names
keyUsage = keyEncipherment,dataEncipherment,digitalSignature
extendedKeyUsage = serverAuth

[alt_names]
DNS.1 = localhost
DNS.2 = *.localhost
IP.1 = 127.0.0.1
IP.2 = ::1
EOF

# Create client certificate configuration  
cat > client.conf << EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = PQC Client
O = Pipy PQC Demo
OU = Client Certificate
C = US

[v3_req]
keyUsage = digitalSignature
extendedKeyUsage = clientAuth
EOF

echo "1. Generating CA private key with ML-DSA-65..."
openssl genpkey \
  -provider oqsprovider \
  -algorithm mldsa65 \
  -out ca-key.pem

echo "2. Creating CA certificate..."
openssl req \
  -provider oqsprovider \
  -new -x509 \
  -key ca-key.pem \
  -out ca-cert.pem \
  -days 365 \
  -config ca.conf

echo "3. Generating server private key with ML-DSA-65..."
openssl genpkey \
  -provider oqsprovider \
  -algorithm mldsa65 \
  -out server-key.pem

echo "4. Creating server certificate signing request..."
openssl req \
  -provider oqsprovider \
  -new \
  -key server-key.pem \
  -out server-csr.pem \
  -config server.conf

echo "5. Signing server certificate with CA..."
openssl x509 \
  -provider oqsprovider \
  -req \
  -in server-csr.pem \
  -CA ca-cert.pem \
  -CAkey ca-key.pem \
  -out server-cert.pem \
  -days 365 \
  -extensions v3_req \
  -extfile server.conf

echo "6. Generating client private key with ML-DSA-65..."
openssl genpkey \
  -provider oqsprovider \
  -algorithm mldsa65 \
  -out client-key.pem

echo "7. Creating client certificate signing request..."
openssl req \
  -provider oqsprovider \
  -new \
  -key client-key.pem \
  -out client-csr.pem \
  -config client.conf

echo "8. Signing client certificate with CA..."
openssl x509 \
  -provider oqsprovider \
  -req \
  -in client-csr.pem \
  -CA ca-cert.pem \
  -CAkey ca-key.pem \
  -out client-cert.pem \
  -days 365 \
  -extensions v3_req \
  -extfile client.conf

echo "9. Cleaning up temporary files..."
rm -f ca-csr.pem server-csr.pem client-csr.pem *.conf

echo "✓ PQC certificates generated successfully!"
echo ""
echo "Generated files:"
echo "  ca-cert.pem       - CA certificate"
echo "  ca-key.pem        - CA private key" 
echo "  server-cert.pem   - Server certificate"
echo "  server-key.pem    - Server private key"
echo "  client-cert.pem   - Client certificate"
echo "  client-key.pem    - Client private key"
echo ""
echo "All certificates use ML-DSA-65 (post-quantum signature algorithm)"

# Copy certificates to the main directory for easy access
cd ..
cp certs/*.pem .

echo "✓ Certificates copied to sample directory"
echo ""
echo "You can now run:"
echo "  pipy server.js   # Start PQC mTLS server"  
echo "  pipy client.js   # Connect with PQC mTLS client"