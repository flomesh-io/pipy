pipy({
  _keys: {
    'key-1': new crypto.PrivateKey(os.readFile('sample-key-rsa.pem')),
    'key-2': new crypto.PrivateKey(os.readFile('sample-key-ecdsa.pem')),
  },

  _services: {
    'service-1': {
      balancer: new algo.RoundRobinLoadBalancer(['127.0.0.1:8080']),
    },
    'service-2': {
      balancer: new algo.RoundRobinLoadBalancer(['127.0.0.1:8081', '127.0.0.1:8082']),
      keys: { 'key-1': true, 'key-2': true },
    },
  },

  _router: new algo.URLRouter({
    '/*': {
      service: 'service-1',
    },
    '/hi/*': {
      service: 'service-2',
      rewrite: [new RegExp('^/hi'), '/hello'],
    },
  }),

  _g: {
    connectionID: 0,
  },

  _connectionPool: new algo.ResourcePool(
    () => ++_g.connectionID
  ),

  _turnDown: false,
  _route: null,
  _service: null,
  _balancerCache: null,
  _target: '',
  _targetCache: null,

  _LOG_URL: new URL('http://127.0.0.1:8123/log'),

  _request: null,
  _requestTime: 0,
  _responseTime: 0,
})

.listen(8000)
  .handleSessionStart(
    () => (
      _balancerCache = new algo.Cache(
        // k is a balancer, v is a target
        (k  ) => k.select(),
        (k,v) => k.deselect(v),
      ),
      _targetCache = new algo.Cache(
        // k is a target, v is a connection ID
        (k  ) => _connectionPool.allocate(k),
        (k,v) => _connectionPool.free(v),
      )
    )
  )
  .handleSessionEnd(
    () => (
      _balancerCache.clear(),
      _targetCache.clear()
    )
  )
  .demuxHTTP('routing')

.pipeline('routing')
  .fork('log-request')
  .handleMessageStart(
    msg => (
      _route = _router.find(
        msg.head.headers.host,
        msg.head.path,
      ),
      _route && (
        _route.rewrite && (
          msg.head.path = msg.head.path.replace(
            _route.rewrite[0],
            _route.rewrite[1],
          )
        ),
        _service = _services[_route.service]
      )
    )
  )
  .link('jwt')
  .link(
    'bypass', () => _turnDown,
    'load-balance', () => Boolean(_service),
    '404'
  )
  .fork('log-response')

.pipeline('jwt')
  .replaceMessage(
    msg => (
      ((
        header,
        jwt,
        kid,
        key,
      ) => (
        _service?.keys ? (
          header = msg.head.headers.authorization || '',
          header.startsWith('Bearer ') && (header = header.substring(7)),
          jwt = new crypto.JWT(header),
          jwt.isValid ? (
            kid = jwt.header?.kid,
            key = _keys[kid],
            key ? (
              _service.keys[kid] ? (
                jwt.verify(key) ? (
                  msg
                ) : (
                  _turnDown = true,
                  new Message({ status: 401 }, 'Invalid signature')
                )
              ) : (
                _turnDown = true,
                new Message({ status: 403 }, 'Access denied')
              )
            ) : (
              _turnDown = true,
              new Message({ status: 401 }, 'Invalid key')
            )
          ) : (
            _turnDown = true,
            new Message({ status: 401 }, 'Invalid token')
          )
        ) : msg
      ))()
    )
  )

.pipeline('load-balance')
  .handleMessageStart(
    () => _target = _balancerCache.get(_service.balancer)
  )
  .link(
    'forward', () => Boolean(_target),
    'no-target'
  )

.pipeline('forward')
  .muxHTTP(
    'connection',
    () => _targetCache.get(_target)
  )

.pipeline('connection')
  .connect(
    () => _target
  )

.pipeline('404')
  .replaceMessage(
    new Message({ status: 404 }, 'No route')
  )

.pipeline('no-target')
  .replaceMessage(
    new Message({ status: 404 }, 'No target')
  )

.pipeline('log-request')
  .handleMessageStart(
    () => _requestTime = Date.now()
  )
  .decompressHTTP()
  .handleMessage(
    '256k',
    msg => _request = msg
  )

.pipeline('log-response')
  .handleMessageStart(
    () => _responseTime = Date.now()
  )
  .decompressHTTP()
  .replaceMessage(
    '256k',
    msg => (
      new Message(
        JSON.encode({
          req: {
            ..._request.head,
            body: _request.body.toString(),
          },
          res: {
            ...msg.head,
            body: msg.body.toString(),
          },
          target: _target,
          reqTime: _requestTime,
          resTime: _responseTime,
          endTime: Date.now(),
          remoteAddr: __inbound.remoteAddress,
          remotePort: __inbound.remotePort,
          localAddr: __inbound.localAddress,
          localPort: __inbound.localPort,
        }).push('\n')
      )
    )
  )
  .merge('log-send')

.pipeline('log-send')
  .pack(
    1000,
    {
      timeout: 5,
    }
  )
  .replaceMessageStart(
    () => new MessageStart({
      method: 'POST',
      path: _LOG_URL.path,
      headers: {
        'Host': _LOG_URL.host,
        'Content-Type': 'application/json',
      }
    })
  )
  .encodeHTTPRequest()
  .connect(
    () => _LOG_URL.host,
    {
      bufferLimit: '8m',
    }
  )

.pipeline('bypass')