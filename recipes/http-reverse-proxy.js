pipy({
  _g: {
    router: null,
    upstreams: null,
    connectionPool: new algo.ResourcePool(
      () => ++_g.connectionCounter
    ),
    connectionCounter: 0,
  },

  _route: undefined,
  _upstream: undefined,
  _upstreamCache: undefined,
  _target: undefined,
  _connection: undefined,
})

// HTTP inbound
.listen(os.env.PIPY_HTTP_PORT || 8000)
  .onSessionStart(
    () => (
      _upstreamCache = new algo.Cache(
        (upstream) => upstream?.balancer?.select?.(),
        (upstream, target) => upstream?.balancer?.deselect?.(target),
      )
    )
  )
  .onSessionEnd(
    () => (
      _upstreamCache.clear(),
      _g.connectionPool.freeTenant(__inbound.id)
    )
  )
  .decodeHttpRequest()
  .demux('per-request-routing')
  .encodeHttpResponse()

// Per-request routing
.pipeline('per-request-routing')
  .onMessageStart(
    e => (
      _route = _g.router?.find?.(e.head.headers.host, e.head.path) || null,
      _upstream = _g.upstreams[_route?.upstream]
    )
  )
  .wait(
    () => _route !== undefined
  )
  .link(
    'no-route', () => _route === null,
    'load-balancing'
  )

// Find a target
.pipeline('load-balancing')
  .onMessageStart(
    () => (
      _target = _upstreamCache.get(_upstream),
      _connection = _g.connectionPool.allocate(_target, __inbound.id)
    )
  )
  .link(
    'pass', () => _target,
    'no-target'
  )

// Pass to upstream
.pipeline('pass')
  .mux(
    'upstream',
    () => _connection
  )

// Long-term upstream connection
.pipeline('upstream')
  .encodeHttpRequest()
  .connect(
    () => _target
  )
  .decodeHttpResponse()

// No route found
.pipeline('no-route')
  .replaceMessage(
    new Message({ status: 404 }, 'No route')
  )

// No target found
.pipeline('no-target')
  .replaceMessage(
    new Message({ status: 404 }, 'No target')
  )

// Check configuration updates
.task('5s')
  .use(
    'modules/config.js',
    'check',
    'http-reverse-proxy.config.json',
    config => (
      _g.router = new algo.URLRouter(config.routes),
      _g.upstreams = Object.fromEntries(
        Object.entries(config.upstreams).map(
          ([k, v]) => ([
            k,
            {
              balancer: new algo.RoundRobinLoadBalancer(v.targets),
              ...v,
            },
          ])
        )
      )
    ),
  )
