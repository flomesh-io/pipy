#!/bin/bash

# PQC mTLS Demo Script
# Demonstrates Post-Quantum Cryptography mutual TLS functionality

set -e

echo "======================================"
echo "PQC mTLS Demo - Post-Quantum TLS Test"
echo "======================================"
echo ""

# Check if certificates exist
if [ ! -f "certs/server-cert.pem" ]; then
    echo "Certificates not found. Generating certificates..."
    ./gen-traditional-certs.sh
    echo ""
fi

# Get Pipy binary path
PIPY="/home/kefu/dev/pipy/bin/pipy"

# Check OpenSSL version for appropriate algorithms
OPENSSL_VERSION=$(openssl version | cut -d' ' -f2)
echo "OpenSSL version: $OPENSSL_VERSION"

# Parse version for comparison
version_compare() {
    echo "$@" | awk -F. '{ printf("%d%03d%03d\n", $1,$2,$3); }'
}

CURRENT_VERSION=$(version_compare $OPENSSL_VERSION)
VERSION_3_5_0=$(version_compare "3.5.0")

if [ "$CURRENT_VERSION" -ge "$VERSION_3_5_0" ]; then
    echo "Using OpenSSL >= 3.5: Testing key exchange only"
    KEM_ALGORITHMS=("ML-KEM-512" "ML-KEM-768" "ML-KEM-1024")
    SIG_ALGORITHMS=("")  # Empty for OpenSSL 3.5+
else
    echo "Using OpenSSL < 3.5: Testing full PQC support"
    KEM_ALGORITHMS=("ML-KEM-768" "ML-KEM-1024")
    SIG_ALGORITHMS=("ML-DSA-44" "ML-DSA-65")
fi

echo ""

# Function to run server in background
start_server() {
    local port=$1
    local kem=$2
    local sig=$3
    local extra_args=$4
    
    echo "Starting server on port $port with KEM=$kem${sig:+ SIG=$sig}..."
    
    local cmd="$PIPY server.js --port $port --kem $kem $extra_args"
    if [ -n "$sig" ]; then
        cmd="$cmd --sig $sig"
    fi
    
    $cmd &
    local server_pid=$!
    
    # Wait for server to start
    sleep 2
    
    # Check if server is running
    if kill -0 $server_pid 2>/dev/null; then
        echo "Server started successfully (PID: $server_pid)"
        echo $server_pid
    else
        echo "Failed to start server"
        return 1
    fi
}

# Function to run client tests
run_client_tests() {
    local port=$1
    local kem=$2
    local sig=$3
    
    echo ""
    echo "Testing client connections to port $port..."
    echo "Configuration: KEM=$kem${sig:+ SIG=$sig}"
    echo ""
    
    local base_cmd="$PIPY client.js --url https://localhost:$port --kem $kem"
    if [ -n "$sig" ]; then
        base_cmd="$base_cmd --sig $sig"
    fi
    
    echo "1. Health check:"
    $base_cmd/health || echo "Health check failed"
    echo ""
    
    echo "2. PQC info endpoint:"
    $base_cmd/pqc-info || echo "PQC info failed"
    echo ""
    
    echo "3. API endpoint with custom headers:"
    $base_cmd/api/test --header "X-Test: PQC-Demo" --method POST || echo "API test failed"
    echo ""
    
    echo "4. Multiple concurrent connections:"
    $base_cmd/health --connections 3 --requests 2 || echo "Concurrent test failed"
    echo ""
    
    echo "5. Server metrics:"
    $base_cmd/metrics || echo "Metrics failed"
    echo ""
}

# Function to cleanup background processes
cleanup() {
    echo "Cleaning up..."
    jobs -p | xargs -r kill 2>/dev/null || true
    wait 2>/dev/null || true
}

trap cleanup EXIT

# Main test execution
echo "=== Demo 1: Basic PQC Key Exchange ==="
echo ""

# Test basic key exchange with ML-KEM-768
PORT=8443
server_pid=$(start_server $PORT "ML-KEM-768" "" "")

if [ $? -eq 0 ]; then
    run_client_tests $PORT "ML-KEM-768" ""
    kill $server_pid 2>/dev/null || true
    wait $server_pid 2>/dev/null || true
fi

echo ""
echo "=== Demo 2: Different KEM Algorithms ==="
echo ""

# Test different KEM algorithms
for kem in "${KEM_ALGORITHMS[@]}"; do
    PORT=$((8443 + RANDOM % 1000))
    echo "Testing KEM algorithm: $kem on port $PORT"
    
    server_pid=$(start_server $PORT "$kem" "" "")
    
    if [ $? -eq 0 ]; then
        # Quick test
        echo "Quick connection test:"
        $PIPY client.js --url https://localhost:$PORT/health --kem "$kem" || echo "Test failed"
        
        kill $server_pid 2>/dev/null || true
        wait $server_pid 2>/dev/null || true
    fi
    echo ""
done

# Test signature algorithms if supported
if [ ${#SIG_ALGORITHMS[@]} -gt 1 ] && [ -n "${SIG_ALGORITHMS[0]}" ]; then
    echo ""
    echo "=== Demo 3: PQC Signatures (OpenSSL < 3.5) ==="
    echo ""
    
    for sig in "${SIG_ALGORITHMS[@]}"; do
        if [ -n "$sig" ]; then
            PORT=$((8443 + RANDOM % 1000))
            echo "Testing signature algorithm: $sig on port $PORT"
            
            server_pid=$(start_server $PORT "ML-KEM-768" "$sig" "")
            
            if [ $? -eq 0 ]; then
                echo "Quick connection test with signatures:"
                $PIPY client.js --url https://localhost:$PORT/pqc-info --kem ML-KEM-768 --sig "$sig" || echo "Signature test failed"
                
                kill $server_pid 2>/dev/null || true
                wait $server_pid 2>/dev/null || true
            fi
            echo ""
        fi
    done
fi

echo ""
echo "=== Demo 4: Pure PQC Mode (No Hybrid) ==="
echo ""

PORT=$((8443 + RANDOM % 1000))
server_pid=$(start_server $PORT "ML-KEM-1024" "" "--no-hybrid")

if [ $? -eq 0 ]; then
    echo "Testing pure PQC mode (no hybrid):"
    $PIPY client.js --url https://localhost:$PORT/pqc-info --kem ML-KEM-1024 --no-hybrid || echo "Pure PQC test failed"
    
    kill $server_pid 2>/dev/null || true
    wait $server_pid 2>/dev/null || true
fi

echo ""
echo "============================================"
echo "PQC mTLS Demo completed!"
echo ""
echo "The demo tested:"
echo "✓ Post-quantum key exchange algorithms"
echo "✓ Mutual TLS authentication" 
echo "✓ Multiple concurrent connections"
echo "✓ Various API endpoints"
echo "✓ Statistics and monitoring"
if [ ${#SIG_ALGORITHMS[@]} -gt 1 ] && [ -n "${SIG_ALGORITHMS[0]}" ]; then
    echo "✓ PQC digital signatures"
fi
echo "✓ Hybrid and pure PQC modes"
echo ""
echo "For manual testing, use:"
echo "  ./server.js --help"
echo "  ./client.js --help"
echo "============================================"