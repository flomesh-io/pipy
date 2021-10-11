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
      cert: new crypto.CertificateChain(pipy.load('server-cert.pem')),
      key: new crypto.PrivateKey(pipy.load('server-key.pem')),
    } : undefined,
  })

.pipeline('tls-offloaded')
  .use(config.plugins, 'connect')
  .demuxHTTP('request')

.pipeline('request')
  .use(config.plugins, 'request', () => !__turnDown)
  .link(
    'bypass', () => __turnDown,
    'no-handler'
  )
  .use(config.plugins.slice().reverse(), 'response')

.pipeline('no-handler')
  .replaceMessage(
    new Message({ status: 404 }, 'No handler')
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('proxy.json')))
