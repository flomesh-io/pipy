var caKeyFile = pipy.load('ca.key')
var caCertFile = pipy.load('ca.crt')

if (caKeyFile && caCertFile) {
  var caKey = new crypto.PrivateKey(caKeyFile)
  var caCert = new crypto.Certificate(caCertFile)
} else {
  var caKey = new crypto.PrivateKey({ type: 'rsa', bits: 2048 })
  var caCert = new crypto.Certificate({
    subject: { CN: 'pipy.flomesh.io' },
    extensions: { basicConstraints: 'CA:TRUE' },
    privateKey: caKey,
    publicKey: new crypto.PublicKey(caKey),
  })
}

println('CA Private Key:')
println(caKey.toPEM().toString())
println('CA Certificate:')
println(caCert.toPEM().toString())

var key = new crypto.PrivateKey({ type: 'rsa', bits: 2048 })
var pkey = new crypto.PublicKey(key)

var cache = new algo.Cache(
  domain => {
    var cert = new crypto.Certificate({
      subject: { CN: domain },
      extensions: { subjectAltName: `DNS:${domain}` },
      days: 7,
      timeOffset: -3600,
      issuer: caCert,
      publicKey: pkey,
      privateKey: caKey,
    })
  
    println('Generated certificate for', domain)
    return { key, cert }
  },
  null, { ttl: 60*60 }
)

export default function (domain) {
  return cache.get(domain)
}
