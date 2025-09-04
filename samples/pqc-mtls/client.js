#!/usr/bin/env pipy

// PQC mTLS Client - Post-Quantum Cryptography mutual TLS client
// Based on samples/stress structure with PQC enhancements

var options = parseOptions({
  defaults: {
    '--url': 'https://localhost:8443',
    '--kem': 'ML-KEM-768',
    '--sig': 'ML-DSA-65',
    '--method': 'GET',
    '--header': [],
    '--connections': 1,
    '--requests': 1,
    '--interval': 0,
    '--timeout': 10,
    '--verify': false,
    '--help': false,
  },
  shorthands: {
    '-u': '--url',
    '-k': '--kem',
    '-s': '--sig', 
    '-X': '--method',
    '-H': '--header',
    '-c': '--connections',
    '-n': '--requests',
    '-i': '--interval',
    '-t': '--timeout',
    '-v': '--verify',
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
      } else if (name === '--no-verify') {
        opts['--verify'] = false
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
      case 'object': (opts[k] ??= []).push(value); break
    }
  }

  return { args, ...defaults, ...opts }
}

if (options['--help']) {
  console.log('PQC mTLS Client - Post-Quantum Cryptography mutual TLS client')
  console.log('')
  console.log('Usage: pipy client.js [options]')
  console.log('')
  console.log('Options:')
  console.log('  -u, --url <url>         Server URL (default: https://localhost:8443)')
  console.log('  -k, --kem <algorithm>   Key exchange algorithm:')
  console.log('                          ML-KEM-512, ML-KEM-768, ML-KEM-1024')
  console.log('  -s, --sig <algorithm>   Signature algorithm (OpenSSL 3.2-3.4 only):')
  console.log('                          ML-DSA-44, ML-DSA-65, ML-DSA-87, etc.')
  console.log('  --no-hybrid             Disable hybrid mode (pure PQC)')
  console.log('  -X, --method <method>   HTTP method (GET, POST, PUT, DELETE)')
  console.log('  -H, --header <header>   Add HTTP header (format: "Name: Value")')
  console.log('  -c, --connections <n>   Number of concurrent connections')
  console.log('  -n, --requests <n>      Number of requests per connection')
  console.log('  -i, --interval <ms>     Interval between requests (milliseconds)')
  console.log('  -t, --timeout <s>       Request timeout in seconds')
  console.log('  -v, --verify            Enable server certificate verification')
  console.log('  -h, --help              Show this help message')
  console.log('')
  console.log('Examples:')
  console.log('  pipy client.js --url https://localhost:8443/health')
  console.log('  pipy client.js --kem ML-KEM-1024 --method POST /api/test')
  console.log('  pipy client.js --connections 5 --requests 10 /pqc-info')
  console.log('  pipy client.js --header "X-Test: PQC" --interval 1000')
  console.log('')
  console.log('Note: Client certificate (certs/client-cert.pem) is automatically used for mTLS.')
  return
}

var url = new URL(options['--url'])
var kemAlgorithm = options['--kem']
var sigAlgorithm = options['--sig']
var hybrid = options['--hybrid']
var method = options['--method']
var connections = options['--connections']
var requests = options['--requests']
var interval = options['--interval']
var timeout = options['--timeout']
var verify = options['--verify']

// Build headers
var headers = options['--header'].map(function (header) {
  var kv = header.split(':')
  var k = kv[0]
  var v = kv[1] || ''
  return [k.trim(), v.trim()]
})

if (!headers.some(([k]) => k.toLowerCase() === 'host')) {
  headers.push(['Host', url.host])
}

// PQC configuration
var pqcConfig = {
  keyExchange: kemAlgorithm,
  hybrid: hybrid
}

if (sigAlgorithm) {
  pqcConfig.signature = sigAlgorithm
}

console.log('PQC mTLS Client Configuration')
console.log('============================')
console.log(`URL: ${url.href}`)
console.log(`Method: ${method}`)
console.log(`Key Exchange: ${kemAlgorithm}`)
if (sigAlgorithm) {
  console.log(`Signature: ${sigAlgorithm}`)
}
console.log(`Hybrid Mode: ${hybrid ? 'enabled' : 'disabled'}`)
console.log(`Connections: ${connections}`)
console.log(`Requests per connection: ${requests}`)
if (interval > 0) {
  console.log(`Request interval: ${interval}ms`)
}
console.log(`Timeout: ${timeout}s`)
console.log(`Certificate verification: ${verify ? 'enabled' : 'disabled'}`)
console.log('')

// Load client certificates
var clientCert = new crypto.CertificateChain(os.readFile('certs/client-cert.pem'))
var clientKey = new crypto.PrivateKey(os.readFile('certs/client-key.pem'))
var caCert = verify ? new crypto.Certificate(os.readFile('certs/ca-cert.pem')) : null

// Statistics
var successCount = 0
var errorCount = 0
var totalLatency = 0
var minLatency = Number.POSITIVE_INFINITY
var maxLatency = 0
var statusCodes = {}
var startTime = Date.now()

// Request template
var requestMessage = new Message({
  method: method,
  path: url.pathname + url.search,
  headers: Object.fromEntries(headers),
}, null)

console.log('Starting requests...')

// Connection variables
var $connId
var $reqId
var $startTime

// TCP connection pipeline
var tcp = pipeline($=>$
  .connect(`${url.hostname}:${url.port || 443}`)
)

// TLS connection pipeline
var tls = pipeline($=>$
  .connectTLS({
    sni: url.hostname,
    certificate: {
      cert: clientCert,
      key: clientKey,
    },
    trusted: verify ? [caCert] : [],
    pqc: pqcConfig,
    verify: verify
  }).to(tcp)
)

// HTTP request/response pipeline
var httpPipeline = pipeline($=>$
  .repeat(() => $reqId < requests).to($=>$
    .onStart(function() {
      $startTime = pipy.now()
      $reqId++
      if (interval > 0 && $reqId > 1) {
        return new Timeout(interval / 1000).wait()
      }
      return new StreamEnd
    })
    .replaceStreamStart(requestMessage)
    .encodeHTTPRequest()
    .pipe(tls)
    .decodeHTTPResponse()
    .handleMessageStart(function(res) {
      var latency = pipy.now() - $startTime
      var status = res.head.status
      
      successCount++
      totalLatency += latency
      minLatency = Math.min(minLatency, latency)
      maxLatency = Math.max(maxLatency, latency)
      statusCodes[status] = (statusCodes[status] || 0) + 1
      
      console.log(`[Conn ${$connId}] [Req ${$reqId}] ${method} ${url.pathname} → ${status} (${latency.toFixed(2)}ms)`)
      
      // Log interesting headers
      var headers = res.head.headers || {}
      if (headers['x-pqc-server']) {
        console.log(`  PQC Server: KEM=${headers['x-kem-algorithm']}, SIG=${headers['x-sig-algorithm']}, Hybrid=${headers['x-hybrid-mode']}`)
      }
    })
    .handleData(function(data) {
      // Optionally log response body for certain endpoints
      if (url.pathname === '/pqc-info' && data.size > 0) {
        try {
          var info = JSON.decode(data.toString())
          console.log(`  Server PQC: ${info.pqc?.keyExchange}, Client: ${info.client?.commonName}`)
        } catch (e) {
          // Ignore JSON parse errors
        }
      }
    })
    .replaceMessage(new StreamEnd)
  )
)

// Main execution pipeline
pipeline($=>$
  .onStart(new StreamEnd)
  .forkJoin(Array(connections)).to($=>$
    .onStart(function(_, i) {
      $connId = i + 1
      $reqId = 0
      console.log(`Starting connection ${$connId}`)
    })
    .pipe(httpPipeline)
    .onEnd(function() {
      console.log(`Connection ${$connId} completed`)
    })
  )
  .replaceStreamStart(function() {
    var totalTime = (Date.now() - startTime) / 1000
    var totalRequests = successCount + errorCount
    var avgLatency = successCount > 0 ? (totalLatency / successCount) : 0
    
    console.log('')
    console.log('Test Results')
    console.log('============')
    console.log(`Total time: ${totalTime.toFixed(2)}s`)
    console.log(`Total requests: ${totalRequests}`)
    console.log(`Successful: ${successCount}`)
    console.log(`Failed: ${errorCount}`)
    console.log(`Success rate: ${totalRequests > 0 ? ((successCount / totalRequests) * 100).toFixed(1) : 0}%`)
    console.log(`Requests/second: ${(totalRequests / totalTime).toFixed(1)}`)
    
    if (successCount > 0) {
      console.log('')
      console.log('Latency Statistics')
      console.log(`  Average: ${avgLatency.toFixed(2)}ms`)
      console.log(`  Min: ${minLatency.toFixed(2)}ms`)
      console.log(`  Max: ${maxLatency.toFixed(2)}ms`)
    }
    
    if (Object.keys(statusCodes).length > 0) {
      console.log('')
      console.log('Status Codes')
      Object.entries(statusCodes).forEach(([code, count]) => {
        console.log(`  ${code}: ${count}`)
      })
    }
    
    console.log('')
    console.log('PQC Configuration Used')
    console.log(`  Key Exchange: ${kemAlgorithm}`)
    if (sigAlgorithm) {
      console.log(`  Signature: ${sigAlgorithm}`)
    }
    console.log(`  Hybrid Mode: ${hybrid ? 'enabled' : 'disabled'}`)
    
    return new StreamEnd
  })
).spawn().then(function() {
  console.log('Client test completed')
})

// Error handling
process.on('uncaughtException', function(err) {
  errorCount++
  console.error('Error:', err.message)
})

process.on('unhandledRejection', function(err) {
  errorCount++
  console.error('Unhandled rejection:', err.message)
})