import config from './config.js'

var cert = new crypto.CertificateChain(
  pipy.load('secret/server-cert.pem')
)

var key = new crypto.PrivateKey(
  pipy.load('secret/server-key.pem')
)

var plugins = config.plugins.map(
  name => (
    pipy.import(`./plugins/${name}.js`).default
  )
)

var $ctx
var $inbound

var main = pipeline($=>$
  .demuxHTTP().to($=>$
    .onStart(
      function() {
        $ctx = {
          inbound: $inbound,
          route: null,
        }
      }
    )
    .pipe(plugins, () => $ctx)
  )
)

pipy.listen(
  config.listen, ($=>$
    .onStart(ib => void ($inbound = ib))
    .pipe(main)
  )
)

pipy.listen(
  config.listenTLS, ($=>$
    .onStart(ib => void ($inbound = ib))
    .acceptTLS({
      certificate: { cert, key }
    })
    .to(main)
  )
)
