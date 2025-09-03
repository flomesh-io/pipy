#!/usr/bin/env pipy

// PQC mTLS Server - Post-Quantum Cryptography mutual TLS demonstration
// Based on samples/serve structure with PQC enhancements

var options = parseOptions({
  defaults: {
    '--port': 8443,
    '--kem': 'ML-KEM-768',
    '--sig': '',
    '--hybrid': true,
    '--log-level': 'info',
    '--help': false,
  },
  shorthands: {
    '-p': '--port',
    '-k': '--kem', 
    '-s': '--sig',
    '-h': '--help',
  },
})

function parseOptions({ defaults, shorthands }) {
  var args = []
  var opts = {}
  var lastOption

  pipy.argv.forEach(function (term, i) {
    if (i === 0) return
    if (lastOption) {
      if (term.startsWith('-')) throw `Value missing for option ${lastOption}`
      addOption(lastOption, term)
      lastOption = undefined
    } else if (term.startsWith('--')) {
      var kv = term.split('=')
      processOption(kv[0], kv[1])
    } else if (term.startsWith('-')) {
      if (term.length === 2) {
        processOption(term)
      } else {
        processOption(term.substring(0, 2), term.substring(2))
      }
    } else {
      args.push(term)
    }
  })

  function processOption(name, value) {
    var k = shorthands[name] || name
    if (!(k in defaults)) throw `invalid option ${name}`
    if (typeof defaults[k] === 'boolean') {
      if (name === '--no-hybrid') {
        opts['--hybrid'] = false
      } else {
        opts[k] = true
      }
    } else if (value) {
      addOption(k, value)
    } else {
      lastOption = k
    }
  }

  function addOption(name, value) {
    var k = shorthands[name] || name
    switch (typeof defaults[k]) {
      case 'number': opts[k] = Number.parseFloat(value); break
      case 'string': opts[k] = value; break
      case 'boolean': opts[k] = (value === 'true'); break
    }
  }

  return { args, ...defaults, ...opts }
}

if (options['--help']) {
  console.log('PQC mTLS Server - Post-Quantum Cryptography mutual TLS demonstration')
  console.log('')
  console.log('Usage: pipy server.js [options]')
  console.log('')
  console.log('Options:')
  console.log('  -p, --port <number>     Server port (default: 8443)')
  console.log('  -k, --kem <algorithm>   Key exchange algorithm:')
  console.log('                          ML-KEM-512, ML-KEM-768, ML-KEM-1024')
  console.log('  -s, --sig <algorithm>   Signature algorithm (OpenSSL 3.2-3.4 only):')
  console.log('                          ML-DSA-44, ML-DSA-65, ML-DSA-87,')
  console.log('                          SLH-DSA-128s, SLH-DSA-128f, etc.')
  console.log('  --no-hybrid             Disable hybrid mode (pure PQC)')
  console.log('  --log-level <level>     Log level: debug, info, warn, error')
  console.log('  -h, --help              Show this help message')
  console.log('')
  console.log('Examples:')
  console.log('  pipy server.js --port 8443 --kem ML-KEM-768')
  console.log('  pipy server.js --kem ML-KEM-1024 --sig ML-DSA-65')
  console.log('  pipy server.js --kem ML-KEM-512 --no-hybrid')
  console.log('')
  console.log('Note: Signature algorithms require OpenSSL 3.2-3.4 with oqs-provider.')
  console.log('      OpenSSL >= 3.5 only supports key exchange algorithms.')
  return
}

var port = options['--port']
var kemAlgorithm = options['--kem'] 
var sigAlgorithm = options['--sig']
var hybrid = options['--hybrid']

// PQC configuration
var pqcConfig = {
  keyExchange: kemAlgorithm,
  hybrid: hybrid
}

if (sigAlgorithm) {
  pqcConfig.signature = sigAlgorithm
}

console.log('Starting PQC mTLS Server')
console.log(`Port: ${port}`)
console.log(`Key Exchange: ${kemAlgorithm}`)
if (sigAlgorithm) {
  console.log(`Signature: ${sigAlgorithm}`)
}
console.log(`Hybrid Mode: ${hybrid ? 'enabled' : 'disabled'}`)
console.log('')

// Load certificates
var serverCert = new crypto.CertificateChain(os.readFile('certs/server-cert.pem'))
var serverKey = new crypto.PrivateKey(os.readFile('certs/server-key.pem'))
var caCert = new crypto.Certificate(os.readFile('certs/ca-cert.pem'))

// Statistics
var requestCount = new stats.Counter('requests_total', ['method', 'status'])
var connectionCount = new stats.Counter('connections_total', ['type'])
var pqcStats = new stats.Counter('pqc_connections', ['kem', 'signature', 'hybrid'])

// Request processing pipeline
var $clientCert
var $requestStart

var httpHandler = pipeline($=>$
  .demuxHTTP().to($=>$
    .handleMessageStart(function(msg) {
      $requestStart = Date.now()
      var method = msg.head.method || 'UNKNOWN'
      var path = msg.head.path || '/'
      
      console.log(`${method} ${path} - Client: ${$clientCert?.subject?.commonName || 'unknown'}`)
      
      // Route handling
      if (path === '/health') {
        return healthEndpoint
      } else if (path === '/metrics') {
        return metricsEndpoint  
      } else if (path === '/pqc-info') {
        return pqcInfoEndpoint
      } else if (path.startsWith('/api/')) {
        return apiEndpoint
      } else {
        return defaultEndpoint
      }
    })
  )
)

var healthEndpoint = pipeline($=>$
  .replaceData()
  .replaceMessage(function() {
    requestCount.withLabels('GET', '200').increase()
    return new Message({
      status: 200,
      headers: { 'content-type': 'application/json' }
    }, JSON.encode({
      status: 'healthy',
      server: 'PQC mTLS Server',
      timestamp: new Date().toISOString(),
      uptime: pipy.now()
    }))
  })
)

var metricsEndpoint = pipeline($=>$
  .replaceData()
  .replaceMessage(function() {
    requestCount.withLabels('GET', '200').increase()
    
    return new Promise(resolve => {
      stats.sum(['requests_total', 'connections_total', 'pqc_connections']).then(metrics => {
        var response = 'PQC mTLS Server Metrics\n'
        response += '=======================\n\n'
        
        Object.entries(metrics).forEach(([name, metric]) => {
          response += `${name}: ${metric.value}\n`
          if (metric.submetrics) {
            metric.submetrics().forEach(sub => {
              response += `  ${sub.label}: ${sub.value}\n`
            })
          }
        })
        
        resolve(new Message({
          status: 200,
          headers: { 'content-type': 'text/plain' }
        }, response))
      })
    })
  })
)

var pqcInfoEndpoint = pipeline($=>$
  .replaceData()
  .replaceMessage(function() {
    requestCount.withLabels('GET', '200').increase()
    return new Message({
      status: 200,
      headers: { 'content-type': 'application/json' }
    }, JSON.encode({
      server: 'PQC mTLS Server',
      pqc: {
        keyExchange: kemAlgorithm,
        signature: sigAlgorithm || null,
        hybrid: hybrid,
        supportedKEM: ['ML-KEM-512', 'ML-KEM-768', 'ML-KEM-1024'],
        supportedSignatures: ['ML-DSA-44', 'ML-DSA-65', 'ML-DSA-87', 'SLH-DSA-128s', 'SLH-DSA-192s', 'SLH-DSA-256s'],
        note: sigAlgorithm ? 'Full PQC support' : 'KEM-only (OpenSSL >= 3.5 compatibility)'
      },
      client: {
        commonName: $clientCert?.subject?.commonName,
        issuer: $clientCert?.issuer?.commonName,
        validFrom: $clientCert?.validFrom,
        validTo: $clientCert?.validTo
      },
      timestamp: new Date().toISOString()
    }))
  })
)

var apiEndpoint = pipeline($=>$
  .replaceData() 
  .replaceMessage(function(msg) {
    var method = msg.head.method
    var path = msg.head.path
    requestCount.withLabels(method, '200').increase()
    
    return new Message({
      status: 200,
      headers: { 
        'content-type': 'application/json',
        'x-pqc-server': 'true',
        'x-kem-algorithm': kemAlgorithm,
        'x-sig-algorithm': sigAlgorithm || 'none',
        'x-hybrid-mode': hybrid.toString()
      }
    }, JSON.encode({
      message: `API endpoint: ${path}`,
      method: method,
      security: 'Post-Quantum mTLS',
      client: $clientCert?.subject?.commonName || 'unknown',
      timestamp: new Date().toISOString(),
      processing_time: Date.now() - $requestStart + 'ms'
    }))
  })
)

var defaultEndpoint = pipeline($=>$
  .replaceData()
  .replaceMessage(function(msg) {
    var method = msg.head.method
    requestCount.withLabels(method, '200').increase()
    
    return new Message({
      status: 200,
      headers: { 'content-type': 'text/html' }
    }, `
<!DOCTYPE html>
<html>
<head>
  <title>PQC mTLS Server</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 40px; }
    .header { color: #2c5aa0; border-bottom: 2px solid #2c5aa0; padding-bottom: 10px; }
    .info { background: #f0f8ff; padding: 15px; border-radius: 5px; margin: 20px 0; }
    .endpoints { background: #f9f9f9; padding: 15px; border-radius: 5px; }
    code { background: #eee; padding: 2px 4px; border-radius: 3px; }
  </style>
</head>
<body>
  <h1 class="header">🔒 PQC mTLS Server</h1>
  <div class="info">
    <h3>Security Configuration</h3>
    <p><strong>Key Exchange:</strong> ${kemAlgorithm}</p>
    ${sigAlgorithm ? `<p><strong>Signature:</strong> ${sigAlgorithm}</p>` : ''}
    <p><strong>Hybrid Mode:</strong> ${hybrid ? 'Enabled' : 'Disabled'}</p>
    <p><strong>Client Certificate:</strong> ${$clientCert?.subject?.commonName || 'Not provided'}</p>
  </div>
  
  <div class="endpoints">
    <h3>Available Endpoints</h3>
    <ul>
      <li><code>/health</code> - Server health check</li>
      <li><code>/metrics</code> - Server metrics and statistics</li>
      <li><code>/pqc-info</code> - PQC configuration and client info</li>
      <li><code>/api/*</code> - API endpoints with PQC headers</li>
    </ul>
  </div>
  
  <p><em>Connected at: ${new Date().toISOString()}</em></p>
</body>
</html>
    `)
  })
)

// Start server
pipy.listen(port, $=>$
  .onStart(function() {
    connectionCount.withLabels('accepted').increase()
    console.log('New connection accepted')
  })
  .acceptTLS({
    certificate: {
      cert: serverCert,
      key: serverKey,
    },
    trusted: [caCert],
    pqc: pqcConfig,
    verify: function(ok, cert) {
      $clientCert = cert
      var cn = cert?.subject?.commonName || 'unknown'
      console.log(`Client certificate verification: ${ok ? 'PASSED' : 'FAILED'} - CN: ${cn}`)
      
      if (ok) {
        connectionCount.withLabels('verified').increase()
        pqcStats.withLabels(kemAlgorithm, sigAlgorithm || 'none', hybrid.toString()).increase()
      } else {
        connectionCount.withLabels('rejected').increase()
      }
      
      return ok
    }
  }).to(httpHandler)
  .onEnd(function() {
    console.log('Connection closed')
  })
)

console.log(`PQC mTLS Server started on port ${port}`)
console.log(`Try: curl -k --cert certs/client-cert.pem --key certs/client-key.pem https://localhost:${port}/`)