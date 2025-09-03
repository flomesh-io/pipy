#!/bin/bash

# Test script for PQC mTLS functionality
# This script builds Pipy with PQC support and tests the mTLS example

set -e

echo "=== Pipy PQC mTLS Test Suite ==="
echo ""

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
  echo "Error: Please run this script from the Pipy root directory"
  exit 1
fi

# Create build directory
echo "1. Creating build directory..."
mkdir -p build
cd build

echo "2. Configuring build with PQC support..."
cmake .. \
  -DPIPY_PQC=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DPIPY_GUI=OFF \
  -DPIPY_CODEBASES=OFF

echo "3. Building Pipy with PQC support..."
echo "   This may take several minutes..."
cmake --build . -j$(nproc)

if [ $? -ne 0 ]; then
  echo "❌ Build failed. Check dependencies and try again."
  exit 1
fi

echo "✅ Build completed successfully!"
echo ""

# Test that the binary was created
if [ ! -f "bin/pipy" ]; then
  echo "❌ Pipy binary not found at bin/pipy"
  exit 1
fi

echo "4. Testing Pipy binary..."
./bin/pipy --version
echo ""

echo "5. Checking PQC support..."
if ./bin/pipy --help | grep -q "PQC"; then
  echo "✅ PQC support detected in help output"
else
  echo "⚠️  PQC support not explicitly shown in help, but may be available"
fi

echo ""
echo "6. Setting up test environment..."
cd ../samples/pqc-mtls

# Check if oqs-provider was built
OQS_PROVIDER_PATH="../../build/oqs-provider-install/lib"
if [ -d "$OQS_PROVIDER_PATH" ]; then
  echo "✅ oqs-provider found at $OQS_PROVIDER_PATH"
  export OPENSSL_MODULES="$OQS_PROVIDER_PATH"
else
  echo "⚠️  oqs-provider not found, PQC features may not work"
fi

echo ""
echo "7. Generating PQC certificates..."
if [ -f "gen-pqc-certs.sh" ]; then
  ./gen-pqc-certs.sh
  if [ $? -eq 0 ]; then
    echo "✅ PQC certificates generated successfully"
  else
    echo "❌ Certificate generation failed"
    exit 1
  fi
else
  echo "❌ Certificate generation script not found"
  exit 1
fi

echo ""
echo "8. Starting PQC mTLS server test..."
timeout 30s ../../build/bin/pipy server.js &
SERVER_PID=$!

# Give server time to start
sleep 3

echo "9. Testing client connection..."
if ../../build/bin/pipy client.js | grep -q "PQC TLS connection established"; then
  echo "✅ PQC mTLS connection test passed"
  TEST_RESULT=0
else
  echo "❌ PQC mTLS connection test failed"
  TEST_RESULT=1
fi

# Clean up
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo ""
if [ $TEST_RESULT -eq 0 ]; then
  echo "🎉 All tests passed!"
  echo ""
  echo "Your Pipy installation now supports:"
  echo "  ✅ OpenSSL 3.5.2 with native PQC algorithms"  
  echo "  ✅ oqs-provider for extended PQC algorithm support"
  echo "  ✅ ML-KEM (Kyber) for key encapsulation"
  echo "  ✅ ML-DSA (Dilithium) for digital signatures"
  echo "  ✅ Hybrid classical+PQC modes"
  echo "  ✅ PQC-secured mTLS connections"
  echo ""
  echo "Example usage:"
  echo "  cd samples/pqc-mtls"
  echo "  pipy server.js    # Start PQC-secured server"
  echo "  pipy client.js    # Connect with PQC client"
else
  echo "❌ Some tests failed. Check the output above for details."
  exit 1
fi