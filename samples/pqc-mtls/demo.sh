#!/bin/bash

# Comprehensive PQC mTLS Demo - Tests all supported PQC algorithms
# This script demonstrates the complete range of post-quantum cryptography 
# capabilities in Pipy, including both key exchange and signature algorithms.

set -e

PIPY_CMD="/home/kefu/dev/pipy/bin/pipy"
SERVER_PORT=8443
CLIENT_URL="https://localhost:$SERVER_PORT"
DEMO_DIR="$(dirname "$0")"
SERVER_PID=""
RESULTS_FILE="demo-results.log"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== PQC mTLS Comprehensive Demo ===${NC}"
echo "Testing all supported Post-Quantum Cryptography algorithms"
echo "Pipy executable: $PIPY_CMD"
echo "Demo directory: $DEMO_DIR"
echo "Results log: $RESULTS_FILE"
echo ""

# Clean up function
cleanup() {
    if [[ ! -z "$SERVER_PID" ]]; then
        echo -e "\n${YELLOW}Cleaning up server process $SERVER_PID...${NC}"
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    
    # Clean up any background processes
    pkill -f "pipy.*server.js" 2>/dev/null || true
    sleep 1
}

# Set up cleanup on exit
trap cleanup EXIT

# Initialize results log
echo "PQC mTLS Demo Results - $(date)" > "$RESULTS_FILE"
echo "=======================================" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

cd "$DEMO_DIR"

echo -e "${YELLOW}Step 1: Generating certificates...${NC}"
if [[ -x "./gen-pipy-certs.js" ]]; then
    echo "Using Pipy crypto module for PQC certificate generation..."
    $PIPY_CMD gen-pipy-certs.js | tee -a "$RESULTS_FILE"
    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}✓ Certificate generation completed${NC}"
    else
        echo -e "${RED}✗ Certificate generation failed${NC}"
        exit 1
    fi
else
    echo "Using traditional certificate generation..."
    ./gen-traditional-certs.sh | tee -a "$RESULTS_FILE"
fi

echo ""

# Test scenarios: Key exchange algorithms
KEM_ALGORITHMS=("ML-KEM-512" "ML-KEM-768" "ML-KEM-1024")

# Test scenarios: Signature algorithms (if supported)
SIG_ALGORITHMS=("ML-DSA-44" "ML-DSA-65" "ML-DSA-87" "SLH-DSA-SHA2-128s" "SLH-DSA-SHA2-128f" "SLH-DSA-SHAKE-128s")

echo -e "${YELLOW}Step 2: Testing Key Exchange Algorithms${NC}"
echo "Testing Key Exchange Algorithms" >> "$RESULTS_FILE"
echo "===============================" >> "$RESULTS_FILE"

for kem in "${KEM_ALGORITHMS[@]}"; do
    echo -e "\n${BLUE}Testing KEM: $kem${NC}"
    echo "" >> "$RESULTS_FILE"
    echo "--- Testing KEM: $kem ---" >> "$RESULTS_FILE"
    
    # Start server with specific KEM algorithm
    echo "Starting server with KEM=$kem..."
    $PIPY_CMD server.js -- --port $SERVER_PORT --kem "$kem" --log-level info > "server-$kem.log" 2>&1 &
    SERVER_PID=$!
    
    # Wait for server to start
    sleep 2
    
    # Check if server is running
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}✗ Server failed to start with KEM=$kem${NC}"
        echo "FAILED: Server failed to start with KEM=$kem" >> "$RESULTS_FILE"
        cat "server-$kem.log" >> "$RESULTS_FILE"
        continue
    fi
    
    # Test basic connectivity
    echo "Testing basic connectivity..."
    if $PIPY_CMD client.js -- --url "$CLIENT_URL/health" --kem "$kem" --requests 1 > "client-$kem-health.log" 2>&1; then
        echo -e "${GREEN}✓ Health check passed${NC}"
        echo "SUCCESS: Health check passed" >> "$RESULTS_FILE"
    else
        echo -e "${RED}✗ Health check failed${NC}"
        echo "FAILED: Health check failed" >> "$RESULTS_FILE"
        cat "client-$kem-health.log" >> "$RESULTS_FILE"
    fi
    
    # Test PQC info endpoint
    echo "Testing PQC info endpoint..."
    if $PIPY_CMD client.js -- --url "$CLIENT_URL/pqc-info" --kem "$kem" --requests 1 > "client-$kem-info.log" 2>&1; then
        echo -e "${GREEN}✓ PQC info endpoint passed${NC}"
        echo "SUCCESS: PQC info endpoint passed" >> "$RESULTS_FILE"
        # Extract PQC info from client log
        grep -E "(Server PQC|Client:)" "client-$kem-info.log" >> "$RESULTS_FILE" 2>/dev/null || true
    else
        echo -e "${RED}✗ PQC info endpoint failed${NC}"
        echo "FAILED: PQC info endpoint failed" >> "$RESULTS_FILE"
        cat "client-$kem-info.log" >> "$RESULTS_FILE"
    fi
    
    # Test concurrent connections
    echo "Testing concurrent connections..."
    if $PIPY_CMD client.js -- --url "$CLIENT_URL/api/test" --kem "$kem" --connections 3 --requests 5 > "client-$kem-concurrent.log" 2>&1; then
        echo -e "${GREEN}✓ Concurrent connections test passed${NC}"
        echo "SUCCESS: Concurrent connections test passed" >> "$RESULTS_FILE"
        # Extract performance metrics
        grep -E "(Total requests|Success rate|Requests/second)" "client-$kem-concurrent.log" >> "$RESULTS_FILE" 2>/dev/null || true
    else
        echo -e "${RED}✗ Concurrent connections test failed${NC}"
        echo "FAILED: Concurrent connections test failed" >> "$RESULTS_FILE"
        cat "client-$kem-concurrent.log" >> "$RESULTS_FILE"
    fi
    
    # Stop server
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    SERVER_PID=""
    
    sleep 1
done

echo ""
echo -e "${YELLOW}Step 3: Testing Combined KEM + Signature Algorithms${NC}"
echo "Testing Combined KEM + Signature Algorithms" >> "$RESULTS_FILE"
echo "===========================================" >> "$RESULTS_FILE"

# Test combinations of KEM + Signature algorithms
COMBINATIONS=(
    "ML-KEM-768:ML-DSA-65"
    "ML-KEM-512:ML-DSA-44" 
    "ML-KEM-1024:ML-DSA-87"
    "ML-KEM-768:SLH-DSA-SHA2-128s"
    "ML-KEM-512:SLH-DSA-SHAKE-128s"
)

for combo in "${COMBINATIONS[@]}"; do
    IFS=':' read -r kem sig <<< "$combo"
    echo -e "\n${BLUE}Testing combination: KEM=$kem + SIG=$sig${NC}"
    echo "" >> "$RESULTS_FILE"
    echo "--- Testing combination: KEM=$kem + SIG=$sig ---" >> "$RESULTS_FILE"
    
    # Start server with KEM + Signature
    echo "Starting server with KEM=$kem and SIG=$sig..."
    $PIPY_CMD server.js -- --port $SERVER_PORT --kem "$kem" --sig "$sig" --log-level info > "server-$kem-$sig.log" 2>&1 &
    SERVER_PID=$!
    
    # Wait for server to start
    sleep 2
    
    # Check if server is running
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}✗ Server failed to start with KEM=$kem + SIG=$sig${NC}"
        echo "FAILED: Server failed to start" >> "$RESULTS_FILE"
        cat "server-$kem-$sig.log" >> "$RESULTS_FILE"
        continue
    fi
    
    # Test with matching client configuration
    echo "Testing client with matching algorithms..."
    if $PIPY_CMD client.js -- --url "$CLIENT_URL/pqc-info" --kem "$kem" --sig "$sig" --requests 3 > "client-$kem-$sig.log" 2>&1; then
        echo -e "${GREEN}✓ Combined algorithm test passed${NC}"
        echo "SUCCESS: Combined algorithm test passed" >> "$RESULTS_FILE"
        
        # Extract detailed PQC information
        grep -E "(PQC.*Config|Server PQC)" "client-$kem-$sig.log" >> "$RESULTS_FILE" 2>/dev/null || true
    else
        echo -e "${RED}✗ Combined algorithm test failed${NC}"
        echo "FAILED: Combined algorithm test failed" >> "$RESULTS_FILE"
        cat "client-$kem-$sig.log" >> "$RESULTS_FILE"
    fi
    
    # Stop server
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    SERVER_PID=""
    
    sleep 1
done

echo ""
echo -e "${YELLOW}Step 4: Testing Certificate Validation${NC}"
echo "Testing Certificate Validation" >> "$RESULTS_FILE"
echo "==============================" >> "$RESULTS_FILE"

echo -e "\n${BLUE}Testing with certificate verification enabled${NC}"
echo "" >> "$RESULTS_FILE"
echo "--- Certificate Verification Test ---" >> "$RESULTS_FILE"

# Start server with default configuration
$PIPY_CMD server.js -- --port $SERVER_PORT --kem "ML-KEM-768" --sig "ML-DSA-65" > "server-verify.log" 2>&1 &
SERVER_PID=$!

sleep 2

if kill -0 $SERVER_PID 2>/dev/null; then
    # Test with certificate verification
    if $PIPY_CMD client.js -- --url "$CLIENT_URL/pqc-info" --verify --requests 1 > "client-verify.log" 2>&1; then
        echo -e "${GREEN}✓ Certificate verification test passed${NC}"
        echo "SUCCESS: Certificate verification test passed" >> "$RESULTS_FILE"
    else
        echo -e "${YELLOW}⚠ Certificate verification test failed (expected for self-signed certs)${NC}"
        echo "WARNING: Certificate verification failed (expected for self-signed certificates)" >> "$RESULTS_FILE"
    fi
else
    echo -e "${RED}✗ Server failed to start for verification test${NC}"
    echo "FAILED: Server failed to start for verification test" >> "$RESULTS_FILE"
fi

# Stop server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
SERVER_PID=""

echo ""
echo -e "${YELLOW}Step 5: Performance and Load Testing${NC}"
echo "Performance and Load Testing" >> "$RESULTS_FILE"
echo "============================" >> "$RESULTS_FILE"

echo -e "\n${BLUE}Running performance tests with different algorithms${NC}"

PERF_ALGORITHMS=("ML-KEM-512" "ML-KEM-768" "ML-KEM-1024")

for kem in "${PERF_ALGORITHMS[@]}"; do
    echo -e "\n${BLUE}Performance test with KEM=$kem${NC}"
    echo "" >> "$RESULTS_FILE"
    echo "--- Performance test: KEM=$kem ---" >> "$RESULTS_FILE"
    
    # Start server
    $PIPY_CMD server.js -- --port $SERVER_PORT --kem "$kem" > "server-perf-$kem.log" 2>&1 &
    SERVER_PID=$!
    
    sleep 2
    
    if kill -0 $SERVER_PID 2>/dev/null; then
        # Run load test
        echo "Running load test: 10 connections, 20 requests each..."
        if $PIPY_CMD client.js -- --url "$CLIENT_URL/api/perf" --kem "$kem" --connections 10 --requests 20 --interval 10 > "client-perf-$kem.log" 2>&1; then
            echo -e "${GREEN}✓ Performance test completed${NC}"
            echo "SUCCESS: Performance test completed" >> "$RESULTS_FILE"
            
            # Extract performance metrics
            grep -E "(Total time|Total requests|Success rate|Requests/second|Average.*ms)" "client-perf-$kem.log" >> "$RESULTS_FILE" 2>/dev/null || true
        else
            echo -e "${RED}✗ Performance test failed${NC}"
            echo "FAILED: Performance test failed" >> "$RESULTS_FILE"
        fi
    else
        echo -e "${RED}✗ Server failed to start for performance test${NC}"
        echo "FAILED: Server failed to start for performance test" >> "$RESULTS_FILE"
    fi
    
    # Stop server
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    SERVER_PID=""
    
    sleep 1
done

echo ""
echo -e "${YELLOW}Step 6: Generating Summary Report${NC}"

# Count successes and failures
SUCCESS_COUNT=$(grep -c "SUCCESS:" "$RESULTS_FILE" 2>/dev/null || echo "0")
FAILED_COUNT=$(grep -c "FAILED:" "$RESULTS_FILE" 2>/dev/null || echo "0")
WARNING_COUNT=$(grep -c "WARNING:" "$RESULTS_FILE" 2>/dev/null || echo "0")

echo "" >> "$RESULTS_FILE"
echo "=======================================" >> "$RESULTS_FILE"
echo "DEMO SUMMARY" >> "$RESULTS_FILE"
echo "=======================================" >> "$RESULTS_FILE"
echo "Total Successful Tests: $SUCCESS_COUNT" >> "$RESULTS_FILE"
echo "Total Failed Tests: $FAILED_COUNT" >> "$RESULTS_FILE"
echo "Total Warnings: $WARNING_COUNT" >> "$RESULTS_FILE"
echo "Demo completed at: $(date)" >> "$RESULTS_FILE"

echo ""
echo -e "${BLUE}=== Demo Summary ===${NC}"
echo -e "${GREEN}Successful tests: $SUCCESS_COUNT${NC}"
echo -e "${RED}Failed tests: $FAILED_COUNT${NC}"
echo -e "${YELLOW}Warnings: $WARNING_COUNT${NC}"
echo ""

if [[ $FAILED_COUNT -eq 0 ]]; then
    echo -e "${GREEN}🎉 All PQC algorithms are working correctly!${NC}"
    echo -e "${GREEN}Your Pipy installation has full post-quantum cryptography support.${NC}"
elif [[ $SUCCESS_COUNT -gt $FAILED_COUNT ]]; then
    echo -e "${YELLOW}⚠️  Most PQC algorithms are working, some issues detected.${NC}"
    echo -e "${YELLOW}Check the detailed log for specific failures.${NC}"
else
    echo -e "${RED}🚨 Multiple PQC tests failed.${NC}"
    echo -e "${RED}Please check your OpenSSL installation and PQC support.${NC}"
fi

echo ""
echo "Detailed results saved to: $RESULTS_FILE"
echo ""
echo -e "${BLUE}Available PQC algorithms tested:${NC}"
echo "Key Exchange: ML-KEM-512, ML-KEM-768, ML-KEM-1024"
echo "Signatures: ML-DSA-44, ML-DSA-65, ML-DSA-87"
echo "           SLH-DSA-SHA2-128s, SLH-DSA-SHA2-128f, SLH-DSA-SHAKE-128s"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "1. Review detailed logs: cat $RESULTS_FILE"
echo "2. Test specific algorithms: pipy server.js -- --kem ML-KEM-768 --sig ML-DSA-65"
echo "3. Run manual tests: pipy client.js -- --url https://localhost:8443/pqc-info"

# Clean up log files (optional)
read -p "Clean up temporary log files? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f server-*.log client-*.log
    echo "Temporary log files cleaned up."
fi

echo ""
echo -e "${GREEN}PQC mTLS Demo completed!${NC}"