# PQC mTLS Demo - Post-Quantum Cryptography with Pipy

This directory contains a comprehensive demonstration of Post-Quantum Cryptography (PQC) support in Pipy, featuring mutual TLS authentication with quantum-resistant algorithms.

## Overview

Post-Quantum Cryptography represents the next generation of cryptographic security, designed to protect against attacks from both classical and quantum computers. This demo showcases:

- **ML-KEM** (Module-Lattice-Based Key Encapsulation) - NIST standardized key exchange
- **ML-DSA** (Module-Lattice-Based Digital Signature Algorithm) - NIST standardized signatures  
- **SLH-DSA** (Stateless Hash-Based Digital Signature Algorithm) - Hash-based signatures
- **Hybrid Mode** - Combines classical and post-quantum algorithms for transition security

## Quick Start

### 1. Generate Certificates
```bash
# Generate certificates (works with all OpenSSL versions)
./gen-traditional-certs.sh
```

### 2. Run Complete Demo
```bash
# Automated demo with multiple test scenarios
./demo.sh
```

### 3. Manual Testing

**Start Server:**
```bash
# Basic PQC server (OpenSSL 3.5+ compatible)
pipy server.js -- --port 8443 --kem ML-KEM-768

# Full PQC server (if signatures are supported)
pipy server.js -- --kem ML-KEM-1024 --sig ML-DSA-65

# Pure PQC mode (no hybrid)
pipy server.js -- --kem ML-KEM-512 --no-hybrid
```

**Test with Client:**
```bash
# Basic health check
pipy client.js -- --url https://localhost:8443/health

# PQC configuration info
pipy client.js -- --url https://localhost:8443/pqc-info --kem ML-KEM-768

# Load testing
pipy client.js -- --connections 5 --requests 10 --interval 100

# API testing with custom headers
pipy client.js -- --url https://localhost:8443/api/test --method POST \
  --header "X-Custom: Value" --kem ML-KEM-1024
```

## Files Description

### Core Implementation
- **`server.js`** - PQC mTLS server based on `samples/serve` structure
- **`client.js`** - PQC mTLS client based on `samples/stress` structure  
- **`demo.sh`** - Automated test suite demonstrating all features

### Certificate Management
- **`gen-traditional-certs.sh`** - Version-aware certificate generation
- **`gen-pqc-certs.sh`** - Legacy PQC certificate script (for reference)
- **`certs/`** - Certificate storage directory with traditional X.509 certificates

## Server Features

### Endpoints
- **`/`** - Interactive HTML dashboard showing PQC configuration
- **`/health`** - Server health check (JSON)
- **`/pqc-info`** - Detailed PQC configuration and client certificate info
- **`/api/*`** - API endpoints with PQC headers
- **`/metrics`** - Server statistics and connection metrics

### Configuration Options
```bash
pipy server.js -- [options]

Options:
  -p, --port <number>     Server port (default: 8443)
  -k, --kem <algorithm>   Key exchange algorithm:
                          ML-KEM-512, ML-KEM-768, ML-KEM-1024
  -s, --sig <algorithm>   Signature algorithm (OpenSSL 3.2-3.4 only):
                          ML-DSA-44, ML-DSA-65, ML-DSA-87,
                          SLH-DSA-128s, SLH-DSA-128f, etc.
  --no-hybrid             Disable hybrid mode (pure PQC)
  --log-level <level>     Log level: debug, info, warn, error
  -h, --help              Show help message
```

## Client Features

### Testing Capabilities
- Multiple concurrent connections
- Configurable request patterns
- Latency and throughput metrics
- Custom HTTP headers
- Certificate verification options

### Configuration Options
```bash
pipy client.js -- [options]

Options:
  -u, --url <url>         Server URL (default: https://localhost:8443)
  -k, --kem <algorithm>   Key exchange algorithm
  -s, --sig <algorithm>   Signature algorithm  
  --no-hybrid             Disable hybrid mode
  -X, --method <method>   HTTP method (GET, POST, PUT, DELETE)
  -H, --header <header>   Add HTTP header ("Name: Value")
  -c, --connections <n>   Number of concurrent connections
  -n, --requests <n>      Number of requests per connection
  -i, --interval <ms>     Interval between requests
  -t, --timeout <s>       Request timeout
  -v, --verify            Enable certificate verification
  -h, --help              Show help message
```

## OpenSSL Version Compatibility

### OpenSSL >= 3.5.0 (Built-in PQC) - **TESTED AND WORKING**
- ✅ **Key Exchange**: ML-KEM-512, ML-KEM-768, ML-KEM-1024
- ❌ **Signatures**: Not available due to OID conflicts
- 🔧 **Configuration**: Key exchange only, signatures ignored
- **Verified Algorithms**: X25519MLKEM768, MLKEM768, SecP384r1MLKEM1024, MLKEM1024

### OpenSSL 3.2.0 - 3.4.x (with oqs-provider)
- ✅ **Key Exchange**: All ML-KEM algorithms
- ✅ **Signatures**: ML-DSA-44/65/87, SLH-DSA variants  
- 🔧 **Configuration**: Full PQC support

### OpenSSL 3.2.0 - 3.4.x (without oqs-provider)
- ✅ **Key Exchange**: Limited support if built-in
- ❌ **Signatures**: Not available
- 🔧 **Configuration**: Traditional certificates with limited PQC

## Verified PQC Implementation

### Successful Test Results
Our implementation has been successfully tested with OpenSSL 3.5.2:

- **✅ PQC Key Exchange**: X25519MLKEM768 hybrid algorithm confirmed working
- **✅ mTLS Authentication**: Client certificate verification successful
- **✅ TLS 1.3**: Full TLS 1.3 handshake with PQC algorithms
- **✅ Certificate Validation**: Traditional X.509 certificates work perfectly
- **✅ HTTP Processing**: Complete request/response cycle functional

### Connection Details (Verified)
```
SSL connection using TLSv1.3 / TLS_AES_256_GCM_SHA384 / X25519MLKEM768 / id-ecPublicKey
```

## Example Usage Scenarios

### Scenario 1: Basic PQC Testing (OpenSSL 3.5+) - **VERIFIED**
```bash
# Terminal 1: Start server
pipy server.js -- --kem ML-KEM-768

# Terminal 2: Test connection
pipy client.js -- /health
pipy client.js -- /pqc-info
```

### Scenario 2: Hybrid vs Pure PQC Comparison
```bash
# Hybrid mode (recommended for production)
pipy server.js -- --kem ML-KEM-768 --hybrid

# Pure PQC mode (for testing)
pipy server.js -- --kem ML-KEM-768 --no-hybrid
```

### Scenario 3: Load Testing with PQC
```bash
# Terminal 1: Start server
pipy server.js -- --port 8443

# Terminal 2: Stress test
pipy client.js -- --connections 10 --requests 50 --interval 10 /api/load-test
```

### Scenario 4: Certificate-based Testing
```bash
# Test with curl (requires client certificates)
curl -k --cert certs/client-cert.pem --key certs/client-key.pem \
  https://localhost:8443/pqc-info

# Expected: JSON response with PQC configuration and client cert details
```

## Security Features

### Mutual TLS Authentication
- Client certificates required for all connections
- Server certificate validation (optional in client)
- CA-based trust chain verification with traditional X.509 certificates

### Post-Quantum Algorithms
- **Key Exchange**: NIST-approved ML-KEM algorithms (512, 768, 1024)
- **Digital Signatures**: ML-DSA and SLH-DSA support (version dependent)
- **Hybrid Security**: Classical + PQC for migration safety

### Monitoring and Observability
- Connection statistics with PQC algorithm tracking
- Request/response metrics with latency measurement
- Client certificate information logging
- Algorithm negotiation success/failure tracking

## Troubleshooting

### Common Issues

**Certificate Errors:**
```bash
# Regenerate certificates
./gen-traditional-certs.sh
```

**Connection Failures:**
- Check OpenSSL version compatibility with `openssl version`
- Verify certificate paths exist in `certs/` directory
- Ensure matching PQC configurations between client and server
- Check firewall/port accessibility

**Algorithm Not Supported:**
- Verify OpenSSL version supports requested algorithms
- Use `openssl list -tls-groups` to check available PQC groups
- For OpenSSL 3.5+: Only key exchange algorithms are supported

### Debug Mode
```bash
# Server with debug logging
pipy server.js -- --log-level debug

# Check available PQC algorithms
openssl list -tls-groups | grep -i kem
```

### Expected Output
Successful PQC connection will show:
```
SSL connection using TLSv1.3 / TLS_AES_256_GCM_SHA384 / X25519MLKEM768
```

## Development Notes

This implementation follows Pipy's architectural patterns:
- **Server**: Based on `samples/serve` with routing and service separation
- **Client**: Based on `samples/stress` with concurrent connection handling
- **Configuration**: Command-line argument parsing with comprehensive help
- **Statistics**: Built-in metrics collection and reporting
- **Error Handling**: Graceful degradation and informative error messages

### Key Implementation Details
- **Algorithm Mapping**: Proper mapping from ML-KEM-XXX to OpenSSL MLKEMXXX
- **Hybrid Mode**: X25519MLKEM768 for optimal security/performance balance
- **Certificate Compatibility**: Uses traditional X.509 certificates (not PQC certificates)
- **Version Detection**: Runtime OpenSSL version detection and feature adaptation

The code demonstrates best practices for:
- PQC algorithm selection and configuration
- Certificate-based mutual authentication
- Pipeline-based request processing
- Concurrent connection management
- Performance monitoring and reporting

---

**Note**: This demo requires Pipy with PQC support enabled. The implementation has been tested and verified working with OpenSSL 3.5.2, providing quantum-resistant key exchange while maintaining compatibility with traditional certificate infrastructure.