(config =>

pipy({
  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => (
          ((balancer => (
            balancer = new algo[v.alg || 'RoundRobinLoadBalancer'](v.targets),
            [k, {
              balancer,
              cache: v.sticky && new algo.Cache(
                () => balancer.select()
              )
            }]
            ))()
          )
        )
      )
    )
  ),

  _service: null,
  _serviceCache: null,
  _target: '',
  _targetCache: null,

  _g: {
    connectionID: 0,
  },

  _connectionPool: new algo.ResourcePool(
    () => ++_g.connectionID
  ),

  _selectKey: null,
  _select: (service, key) => (
    service.cache && key ? (
      service.cache.get(key)
    ) : (
      service.balancer.select()
    )
  ),
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'router',
})

.pipeline('session')
  .handleStreamStart(
    () => (
      _serviceCache = new algo.Cache(
        // k is a service, v is a target
        (k  ) => _select(k, _selectKey),
        (k,v) => k.balancer.deselect(v),
      ),
      _targetCache = new algo.Cache(
        // k is a target, v is a connection ID
        (k  ) => _connectionPool.allocate(k),
        (k,v) => _connectionPool.free(v),
      )
    )
  )
  .handleStreamEnd(
    () => (
      _targetCache.clear(),
      _serviceCache.clear()
    )
  )

.pipeline('request')
  .handleMessageStart(
    (msg) => (
      _selectKey = msg.head?.headers?.['authorization'],
      _service = _services[__serviceID],
      _service && (_target = _serviceCache.get(_service)),
      _target && (__turnDown = true)
    )
  )
  .link(
    'forward', () => Boolean(_target),
    ''
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

)(JSON.decode(pipy.load('config/balancer.json')))
