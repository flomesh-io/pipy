((
  lb = new algo.RoundRobinLoadBalancer([
    'localhost:8080',
    'localhost:8081',
    'localhost:8082',
  ]),

  lb2 = new algo.RoundRobinLoadBalancer([
    'localhost:8083',
    'localhost:8084',
    'localhost:8085',
  ]),

) =>

pipy({
  _target: null,
  _protocol: undefined,
  _selectProtocol: null,
})

.repeat(
  [8080, 8081, 8082], ($, port) => ($
    .listen(port)
    .serveHTTP(
      () => new Message(`${__inbound.id}:${port}\n`)
    )
  )
)

.repeat(
  [8083, 8084, 8085], ($, port) => ($
    .listen(port)
    .acceptTLS({
      certificate: {
        cert: new crypto.Certificate(pipy.load('server-cert.pem')),
        key: new crypto.PrivateKey(pipy.load('server-key.pem')),
      },
      alpn: port == 8083 ? ['http/1.1', 'h2'] : ['http/1.1'],
    }).to(
      $=>$
      .detectProtocol(proto => _protocol = proto)
      .serveHTTP(
        () => new Message(`${_protocol}:${port}\n`)
      )
    )
  )
)

.listen(8000)
.demuxHTTP().to(
  $=>$
  .muxHTTP(
    () => (_target = lb.borrow().id),
    { version: 2 }
  ).to(
    $=>$.connect(
      () => _target,
      { idleTimeout: 5 }
    )
  )
)

.listen(8443)
.acceptTLS({
  certificate: {
    cert: new crypto.Certificate(pipy.load('server-cert.pem')),
    key: new crypto.PrivateKey(pipy.load('server-key.pem')),
  }
}).to(
  $=>$.demuxHTTP().to(
    $=>$
    .muxHTTP(
      () => (_target = lb2.borrow().id),
      { version: () => new Promise(f => _selectProtocol = f) }
    ).to(
      $=>$
      .connectTLS({
        alpn: ['h2', 'http/1.1'],
        handshake: ({ alpn }) => _selectProtocol(alpn),
      }).to(
        $=>$.connect(
          () => _target,
          { idleTimeout: 5 }
        )
      )
    )
  )
)

)()
