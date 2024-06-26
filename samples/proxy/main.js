#!/usr/bin/env pipy

var config = YAML.decode(pipy.load('config.yaml'))
var whiteList = config.proxy.whitelist || []
var blackList = config.proxy.blacklist || []
var useWhiteList = whiteList.length > 0
var useBlackList = blackList.length > 0
var useFakeCA = toBool(config.proxy.useFakeCA)
var enableLog = toBool(config.proxy.enableLog)

var genCert = null
if (useFakeCA === 'true' || useFakeCA === 'yes') {
  genCert = pipy.import('./gen-cert.js').default
}

var whiteFullnames = {}
var whitePostfixes = []
var blackFullnames = {}
var blackPostfixes = []

initNameLUT(whiteList, whiteFullnames, whitePostfixes)
initNameLUT(blackList, blackFullnames, blackPostfixes)

var log = enableLog ? console.log : () => {}

var $proto
var $sni
var $target

var proxy = pipeline($=>$
  .detectProtocol(proto => $proto = proto)
  .pipe(() => (genCert && $proto === 'TLS' ? proxyTLS : proxyTCP))
)

var observe = pipeline($=>$
  .fork().to($=>$
    .decodeHTTPRequest()
    .handleMessageStart(
      function (msg) {
        var head = msg.head
        log('HTTP', head.method, head.path, head.headers.host)
      }
    )
  )
)

var connectTarget = pipeline($=>$
  .pipe(
    () => isTargetAllowed($target) ? 'pass' : 'deny', {
      'pass': $=>$.connect(() => $target),
      'deny': $=>$.replaceStreamStart(new StreamEnd)
    }
  )
)

var proxyTCP = pipeline($=>$
  .pipe(observe)
  .pipe(connectTarget)
)

var proxyTLS = pipeline($=>$
  .acceptTLS({
    certificate: sni => $sni = sni ? genCert(sni) : undefined
  }).to($=>$
    .pipe(observe)
    .connectTLS({ sni: () => $sni }).to($=>$
      .pipe(connectTarget)
    )
  )
)

if (config.proxy.socks) {
  var $target

  pipy.listen(config.proxy.socks.listen, $=>$
    .acceptSOCKS(
      function (req) {
        $target = `${req.domain || req.ip}:${req.port}`
        log('SOCKS', $target)
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
              log('HTTP', head.method, head.path)
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

function toBool(v) {
  if (!v) return false
  switch (v.toString().toLowerCase()) {
    case 'true':
    case 'yes':
    case 'on':
      return true
    default:
      return false
  }
}

function initNameLUT(list, fullnames, postfixes) {
  list.forEach(name => {
    if (name.startsWith('*')) {
      postfixes.push(name.substring(1))
    } else {
      fullnames[name] = true
    }
  })
}

function checkNameLUT(name, fullnames, postfixes) {
  if (name in fullnames) return true
  return postfixes.some(p => name.endsWith(p))
}

function toSet(a) {
  a = a || []
  return Object.fromEntries(a.map(k => [k, true]))
}

function isTargetAllowed(t) {
  if (useWhiteList || useBlackList) {
    var i = t.indexOf(':')
    var h = i > 0 ? t.substring(0, i) : t
    if (useWhiteList) return  checkNameLUT(h, whiteFullnames, whitePostfixes)
    if (useBlackList) return !checkNameLUT(h, blackFullnames, blackPostfixes)
  }
  return true
}
