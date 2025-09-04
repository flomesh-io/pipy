#!/usr/bin/env pipy

// Generate PQC certificates using Pipy's built-in crypto module
// This demonstrates all supported PQC signature algorithms

console.log('=== PQC Certificate Generation with Pipy ===');
console.log('Generating certificates for all supported PQC signature algorithms');
console.log('');

// Ensure certificates directory exists
if (!os.stat('certs')) {
  os.mkdir('certs', { recursive: true });
}

var algorithms = [
  { name: 'ML-DSA-44', keyType: 'mldsa44', description: 'ML-DSA Level 2 (NIST Category 2)' },
  { name: 'ML-DSA-65', keyType: 'mldsa65', description: 'ML-DSA Level 3 (NIST Category 3)' },
  { name: 'ML-DSA-87', keyType: 'mldsa87', description: 'ML-DSA Level 5 (NIST Category 5)' },
  { name: 'SLH-DSA-SHA2-128s', keyType: 'slh-dsa-sha2-128s', description: 'SLH-DSA SHA2 128-bit Small' },
  { name: 'SLH-DSA-SHA2-128f', keyType: 'slh-dsa-sha2-128f', description: 'SLH-DSA SHA2 128-bit Fast' },
  { name: 'SLH-DSA-SHAKE-128s', keyType: 'slh-dsa-shake-128s', description: 'SLH-DSA SHAKE 128-bit Small' }
];

var results = [];

for (var i = 0; i < algorithms.length; i++) {
  var alg = algorithms[i];
  console.log(`Generating ${alg.name} certificates (${alg.description})...`);
  
  try {
    console.log('  1. Generating CA private key...');
    var caPrivateKey = new crypto.PrivateKey({ type: alg.keyType });
    console.log('     ✓ CA private key generated');
    
    console.log('  2. Creating CA certificate...');
    var caCert = new crypto.Certificate({
      subject: {
        commonName: `PQC CA ${alg.name}`,
        organizationName: 'Pipy PQC Demo',
        organizationalUnitName: 'Certificate Authority',
        countryName: 'US'
      },
      extensions: {
        basicConstraints: 'CA:true',
        keyUsage: 'digitalSignature,cRLSign,keyCertSign'
      },
      days: 365,
      privateKey: caPrivateKey,
      publicKey: new crypto.PublicKey(caPrivateKey)
    });
    console.log('     ✓ CA certificate created');
    
    console.log('  3. Generating server private key...');
    var serverPrivateKey = new crypto.PrivateKey({ type: alg.keyType });
    console.log('     ✓ Server private key generated');
    
    console.log('  4. Creating server certificate...');
    var serverCert = new crypto.Certificate({
      subject: {
        commonName: 'localhost',
        organizationName: 'Pipy PQC Demo',
        organizationalUnitName: 'Server Certificate',
        countryName: 'US'
      },
      extensions: {
        subjectAltName: 'DNS:localhost,DNS:*.localhost,IP:127.0.0.1,IP:::1',
        keyUsage: 'keyEncipherment,dataEncipherment,digitalSignature',
        extendedKeyUsage: 'serverAuth'
      },
      days: 365,
      privateKey: caPrivateKey,  // Signed by CA
      publicKey: new crypto.PublicKey(serverPrivateKey),
      issuer: caCert
    });
    console.log('     ✓ Server certificate created');
    
    console.log('  5. Generating client private key...');
    var clientPrivateKey = new crypto.PrivateKey({ type: alg.keyType });
    console.log('     ✓ Client private key generated');
    
    console.log('  6. Creating client certificate...');
    var clientCert = new crypto.Certificate({
      subject: {
        commonName: 'pqc-client',
        organizationName: 'Pipy PQC Demo',
        organizationalUnitName: 'Client Certificate',
        countryName: 'US'
      },
      extensions: {
        keyUsage: 'digitalSignature,keyEncipherment',
        extendedKeyUsage: 'clientAuth'
      },
      days: 365,
      privateKey: caPrivateKey,  // Signed by CA
      publicKey: new crypto.PublicKey(clientPrivateKey),
      issuer: caCert
    });
    console.log('     ✓ Client certificate created');
    
    console.log('  7. Saving certificates to files...');
    var prefix = `certs/${alg.keyType}`;
    
    // Save CA files
    os.writeFile(`${prefix}-ca-cert.pem`, caCert.toPEM());
    os.writeFile(`${prefix}-ca-key.pem`, caPrivateKey.toPEM());
    
    // Save server files
    os.writeFile(`${prefix}-server-cert.pem`, serverCert.toPEM());
    os.writeFile(`${prefix}-server-key.pem`, serverPrivateKey.toPEM());
    
    // Save client files
    os.writeFile(`${prefix}-client-cert.pem`, clientCert.toPEM());
    os.writeFile(`${prefix}-client-key.pem`, clientPrivateKey.toPEM());
    
    console.log('     ✓ All certificates saved');
    console.log(`✅ ${alg.name} certificate generation: COMPLETED`);
    
    results.push({ algorithm: alg.name, status: 'SUCCESS', files: 6 });
    
  } catch (error) {
    console.log('     ✗ Error: ' + error.message);
    console.log(`❌ ${alg.name} certificate generation: FAILED`);
    results.push({ algorithm: alg.name, status: 'FAILED', error: error.message });
  }
  
  console.log('');
}

// Create default certificate links for easy testing
console.log('Creating default certificate links...');
try {
  // Use ML-DSA-65 as default
  var defaultAlg = 'mldsa65';
  
  os.writeFile('certs/ca-cert.pem', os.readFile(`certs/${defaultAlg}-ca-cert.pem`));
  os.writeFile('certs/ca-key.pem', os.readFile(`certs/${defaultAlg}-ca-key.pem`));
  os.writeFile('certs/server-cert.pem', os.readFile(`certs/${defaultAlg}-server-cert.pem`));
  os.writeFile('certs/server-key.pem', os.readFile(`certs/${defaultAlg}-server-key.pem`));
  os.writeFile('certs/client-cert.pem', os.readFile(`certs/${defaultAlg}-client-cert.pem`));
  os.writeFile('certs/client-key.pem', os.readFile(`certs/${defaultAlg}-client-key.pem`));
  
  console.log('✓ Default certificates created (using ML-DSA-65)');
} catch (error) {
  console.log('✗ Failed to create default certificates: ' + error.message);
}

console.log('');

// Summary
console.log('=== Certificate Generation Summary ===');
var successCount = 0;
var failCount = 0;

for (var j = 0; j < results.length; j++) {
  var result = results[j];
  if (result.status === 'SUCCESS') {
    successCount++;
    console.log(`✅ ${result.algorithm}: ${result.files} certificate files generated`);
  } else {
    failCount++;
    console.log(`❌ ${result.algorithm}: ${result.error || 'unknown error'}`);
  }
}

console.log('');
console.log(`Total algorithms tested: ${algorithms.length}`);
console.log(`Successful: ${successCount}`);
console.log(`Failed: ${failCount}`);

if (successCount === algorithms.length) {
  console.log('🎉 All PQC certificate algorithms working perfectly!');
  console.log('');
  console.log('Generated certificate files:');
  console.log('- Default files: ca-cert.pem, ca-key.pem, server-cert.pem, server-key.pem, client-cert.pem, client-key.pem');
  console.log('- Algorithm-specific files: {algorithm}-{type}-{cert|key}.pem');
} else if (successCount > 0) {
  console.log('⚠️  Partial success - some algorithms working');
} else {
  console.log('🚨 All certificate generation failed - check PQC implementation');
}

console.log('');
console.log('Certificate generation completed.');