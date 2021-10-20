(config =>

pipy({
  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => [
          k, new algo.RoundRobinLoadBalancer(v)
        ]
      )
    )
  ),

  _balancer: null,
  _balancerCache: null,
  _target: '',
  _targetCache: null,

  _g: {
    connectionID: 0,
  },

  _connectionPool: new algo.ResourcePool(
    () => ++_g.connectionID
  ),
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'router',
})

.pipeline('session')
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
      _targetCache.clear(),
      _balancerCache.clear()
    )
  )

.pipeline('request')
  .handleMessageStart(
    () => (
      _balancer = _services[__serviceID],
      _balancer && (_target = _balancerCache.get(_balancer)),
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
