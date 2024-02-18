((
  config = pipy.solve('config.js'),

) => pipy()

  .export('main', {
    __route: undefined,
    __isTLS: false,
  })

  .listen(config.listen)
  .link('inbound-http')

  .listen(config.listenTLS)
  .onStart(() => void(__isTLS = true))
  .acceptTLS({
    certificate: config.listenTLS ? {
      cert: new crypto.CertificateChain(pipy.load('secret/server-cert.pem')),
      key: new crypto.PrivateKey(pipy.load('secret/server-key.pem')),
    } : undefined,
  }).to('inbound-http')

  .pipeline('inbound-http')
  .demuxHTTP().to(
    $=>$.chain(config.plugins)
  )

)()
