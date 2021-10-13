pipy({
  _router: new algo.URLRouter({
    '/*'   : new algo.RoundRobinLoadBalancer(['127.0.0.1:8080']),
    '/hi/*': new algo.RoundRobinLoadBalancer(['127.0.0.1:8081', '127.0.0.1:8082']),
  }),

  _g: {
    connectionID: 0,
  },

  _connectionPool: new algo.ResourcePool(
    () => ++_g.connectionID
  ),

  _balancer: null,
  _balancerCache: null,
  _target: '',
  _targetCache: null,
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
  .demuxHTTP('request')

.pipeline('request')
  .handleMessageStart(
    msg => (
      _balancer = _router.find(
        msg.head.headers.host,
        msg.head.path,
      )
    )
  )
  .link(
    'load-balance', () => Boolean(_balancer),
    '404'
  )

.pipeline('load-balance')
  .handleMessageStart(
    () => _target = _balancerCache.get(_balancer)
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
