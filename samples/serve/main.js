((
  config = JSON.decode(pipy.load('config.json')),
  response404 = new Message({ status: 404 }, 'Not found'),
) =>

pipy({
  _service: undefined,
  _target: undefined,
})

.repeat(config.http, (
  ($, { listen, services, routes }, i) => (
    (
      router = new algo.URLRouter(routes),
    ) => (
      Object.values(services).forEach(
        s => select(s.type,
          'proxy', () => Object.assign(s, {
            balancer: new algo.RoundRobinLoadBalancer(s.targets),
            rewrite: s.rewrite && (
              (
                pat = new RegExp(s.rewrite.pattern),
                rep = s.rewrite.replace,
              ) => (
                head => head.path = head.path.replace(pat, rep)
              )
            )(),
          }),
          'files', () => s.files = Object.fromEntries(
            pipy.list(s.path).map(k => [`/${k}`, http.File.from(`${s.path}/${k}`)]).concat([
              ['/', http.File.from(`${s.path}/index.html`)]
            ])
          )
        )
      ),
      $
      .repeat(listen.tcp.ports, ($, p) => $.listen(p).link(`http-tcp-${i}`))
      .repeat(listen.tls.ports, ($, p) => $.listen(p).link(`http-tls-${i}`))
      .pipeline(`http-tls-${i}`)
        .acceptTLS({
          certificate: {
            cert: new crypto.Certificate(pipy.load(listen.tls.certificates.cert)),
            key: new crypto.PrivateKey(pipy.load(listen.tls.certificates.key)),
          }
        }).to(`http-tcp-${i}`)
      .pipeline(`http-tcp-${i}`)
        .demuxHTTP().to(
          $=>$
          .branchMessageStart(
            ({ head }) => (
              _service = services[router.find(head.headers.host, head.path)],
              _service?.type === 'files'
            ), (
              $=>$.replaceMessage(
                ({ head }) => _service.files[head.path]?.toMessage?.(head.headers['accept-encoding']) || response404
              )
            ),
            ({ head }) => _service?.type === 'proxy' && !void(_service.rewrite?.(head)), (
              $=>$.muxHTTP(() => _target = _service.balancer.borrow()).to(
                $=>$.connect(() => _target.id)
              )
            ), (
              $=>$.replaceMessage(response404)
            )
          )
        )
    )
  )()
))

.repeat(config.tcp, (
  ($, { listen, targets }, i) => (
    (
      balancer = new algo.RoundRobinLoadBalancer(targets),
    ) => (
      $
      .repeat(listen, ($, p) => $.listen(p).link(`tcp-${i}`))
      .pipeline(`tcp-${i}`)
        .onStart(
          () => (
            _target = balancer.borrow(),
            new Data
          )
        )
        .branch(
          () => _target, (
            $=>$.connect(() => _target.id)
          ), (
            $=>$.replaceStreamStart(new StreamEnd)
          )
        )
    )
  )()
))

)()
