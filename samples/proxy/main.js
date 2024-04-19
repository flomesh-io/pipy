#!/usr/bin/env pipy

import genCert from './gen-cert.js'

var config = YAML.decode(pipy.load('config.yaml'))

var $proto
var $target

var proxy = pipeline($=>$
  .detectProtocol(proto => $proto = proto)
  .pipe(() => ($proto === 'TLS' ? proxyTLS : proxyTCP))
)

var proxyTCP = pipeline($=>$
  .fork().to($=>$
    .decodeHTTPRequest()
    .handleMessageStart(
      function (msg) {
        var head = msg.head
        println('HTTP', head.method, head.path, head.headers.host)
      }
    )
  )
  .connect(() => $target)
)

var proxyTLS = pipeline($=>$
  .acceptTLS({
    certificate: sni => sni ? genCert(sni) : undefined
  }).to($=>$
    .connectTLS().to(proxyTCP)
  )
)

if (config.proxy.socks) {
  var $target

  pipy.listen(config.proxy.socks.listen, $=>$
    .acceptSOCKS(
      function (req) {
        $target = `${req.domain || req.ip}:${req.port}`
        println('SOCKS', $target)
        return true
      }
    ).to(proxy)
  )
}

if (config.proxy.http) {
  var $target
  var $host

  pipy.listen(config.proxy.http.listen, $=>$
    .demuxHTTP().to($=>$
      .pipe(
        function (req) {
          if (req instanceof MessageStart) {
            var head = req.head
            if (head.method === 'CONNECT') {
              $target = req.head.path
              return 'tunnel'
            } else {
              var url = new URL(head.path)
              $host = `${url.hostname}:${url.port}`
              println('HTTP', head.method, head.path)
              return 'forward'
            }
          }
        }, {
          'tunnel': ($=>$
            .acceptHTTPTunnel(() => new Message({ status: 200 })).to(proxy)
          ),
          'forward': ($=>$
            .muxHTTP(() => $host).to($=>$.connect(() => $host))
          )
        }
      )
    )
  )
}
