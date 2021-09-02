pipy({
  _router: new algo.URLRouter({
    '/*'   : '127.0.0.1:8080',
    '/hi/*': '127.0.0.1:8081',
  }),

  _g: {
    connectionID: 0,
  },

  _connectionPool: new algo.ResourcePool(
    () => ++_g.connectionID
  ),

  _target: '',
  _targetCache: null,
})

.listen(8000)
  .handleSessionStart(
    () => (
      _targetCache = new algo.Cache(
        // k is a target, v is a connection ID
        (k  ) => _connectionPool.allocate(k),
        (k,v) => _connectionPool.free(v),
      )
    )
  )
  .handleSessionEnd(
    () => (
      _targetCache.clear()
    )
  )
  .demuxHTTP('routing')

.pipeline('routing')
  .handleMessageStart(
    msg => (
      _target = _router.find(
        msg.head.headers.host,
        msg.head.path,
      )
    )
  )
  .link(
    'forward', () => Boolean(_target),
    '404'
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