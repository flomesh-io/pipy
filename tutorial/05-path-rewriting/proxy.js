pipy({
  _services: {
    'service-1': {
      balancer: new algo.RoundRobinLoadBalancer(['127.0.0.1:8080']),
    },
    'service-2': {
      balancer: new algo.RoundRobinLoadBalancer(['127.0.0.1:8081', '127.0.0.1:8082']),
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

  _route: null,
  _service: null,
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
  .link(
    'load-balance', () => Boolean(_service),
    '404'
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
