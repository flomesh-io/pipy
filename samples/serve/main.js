var config = YAML.decode(pipy.load('config.yml'))
var page404 = new Message({ status: 404 }, 'Not found')

var $service
var $conn

config.tcp.forEach(
  function ({ listen, targets }) {
    var balancer = new algo.LoadBalancer(targets)
    listen.forEach(
      function (port) {
        pipy.listen(port, $=>$
          .onStart(
            function () {
              $conn = balancer.allocate()
              return $conn ? new Data : new StreamEnd
            }
          )
          .connect(() => $conn.target)
          .onEnd(() => $conn?.free?.())
        )
      }
    )
  }
)

config.http.forEach(
  function ({ listen, services, routes }) {
    var router = new algo.URLRouter(routes)

    Object.values(services).forEach(
      function (service) {
        switch (service.type) {
          case 'proxy':
            service.balancer = new algo.LoadBalancer(service.targets)
            service.version = (service.protocol === 'http2' ? 2 : 1)
            var re = service.rewrite
            if (re) {
              var p = new RegExp(re.pattern)
              var r = re.replace
              service.rewrite = (head) => void (head.path = head.path.replace(p, r))
            }
            break
          case 'files':
            service.files = new http.Directory(service.path)
            break
          default: throw `Unknown service type '${service.type}'`
        }
      }
    )

    var inboundHTTP = pipeline($=>$
      .demuxHTTP().to($=>$
        .pipe(function (evt) {
          if (evt instanceof MessageStart) {
            var head = evt.head
            $service = services[
              router.find(
                head.headers.host,
                head.path,
              )
            ]
            switch ($service?.type) {
              case 'files': return serveFiles
              case 'proxy': return serveProxy
              default: return serve404
            }
          }
        })
      )
    )

    var serveFiles = pipeline($=>$
      .replaceData()
      .replaceMessage(
        req => $service.files.serve(req) || page404
      )
    )

    var serveProxy = pipeline($=>$
      .muxHTTP(
        () => $conn = $service.balancer.allocate(),
        { version: () => $service.version },
      ).to($=>$
        .connect(() => $conn.target)
      )
      .onEnd(() => $conn.free())
    )

    var serve404 = pipeline($=>$
      .replaceData()
      .replaceMessage(page404)
    )

    listen.tcp.ports.forEach(
      function (port) {
        pipy.listen(port, inboundHTTP)
      }
    )

    listen.tls.ports.forEach(
      function (port) {
        pipy.listen(port, $=>$
          .acceptTLS({
            certificate: {
              cert: new crypto.Certificate(pipy.load(listen.tls.certificates.cert)),
              key: new crypto.PrivateKey(pipy.load(listen.tls.certificates.key)),
            }
          }).to(inboundHTTP)
        )
      }
    )
  }
)
