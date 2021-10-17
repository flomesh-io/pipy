(config =>

pipy()

.export('proxy', {
  __turnDown: false,
  __isTLS: false,
})

.listen(config.listen)
  .link('tls-offloaded')

.listen(config.listenTLS)
  .handleSessionStart(
    () => __isTLS = true
  )
  .acceptTLS('tls-offloaded', {
    certificate: config.listenTLS ? {
      cert: new crypto.CertificateChain(pipy.load('secret/server-cert.pem')),
      key: new crypto.PrivateKey(pipy.load('secret/server-key.pem')),
    } : undefined,
  })

.pipeline('tls-offloaded')
  .use(config.plugins, 'session')
  .demuxHTTP('request')

.pipeline('request')
  .use(
    config.plugins,
    'request',
    'response',
    () => !__turnDown
  )

)(JSON.decode(pipy.load('config/proxy.json')))
