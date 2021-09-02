(config =>

pipy({
  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => [
          k,
          {
            balancer: new algo.RoundRobinLoadBalancer(v.targets),
          }
        ]
      )
    )
  ),

  _router: new algo.URLRouter(
    Object.fromEntries(
      Object.entries(config.routes).map(
        ([k, v]) => [
          k,
          {
            ...v,
            rewrite: v.rewrite ? [
              new RegExp(v.rewrite[0]),
              v.rewrite[1],
            ] : undefined,
          }
        ]
      )
    )
  ),

  _g: {
    connectionID: 0,
  },

  _connectionPool: new algo.ResourcePool(
    () => ++_g.connectionID
  ),

  _route: null,
  _balancerCache: null,
  _target: '',
  _targetCache: null,

  _LOG_URL: new URL(config.logURL),

  _request: null,
  _requestTime: 0,
  _responseTime: 0,
})

.export('proxy', {
  __turnDown: false,
  __serviceID: '',
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
        __serviceID = _route.service
      )
    )
  )
  .link('bypass', () => __turnDown, 'ban')
  .link('bypass', () => __turnDown, 'throttle')
  .link('bypass', () => __turnDown, 'jwt')
  .link('bypass', () => __turnDown, 'transform')
  .link(
    'bypass', () => __turnDown,
    'load-balance', () => Boolean(__serviceID),
    '404'
  )
  .fork('log-response')

.pipeline('ban')
  .use('ban.js', 'check')

.pipeline('throttle')
  .use('throttle.js', 'input')

.pipeline('jwt')
  .use('jwt.js', 'verify')

.pipeline('transform')
  .use('transform.js', 'input')

.pipeline('load-balance')
  .handleMessageStart(
    () => _target = _balancerCache.get(_services[__serviceID].balancer)
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

)(JSON.decode(pipy.load('proxy.json')))