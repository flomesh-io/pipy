pipy({
  _router: new algo.URLRouter({
    '/hi/*': 'localhost:8080',
    '/echo': 'localhost:8081',
    '/ip/*': 'localhost:8082',
  }),

  _target: '',
})

.import({
  __turnDown: 'proxy',
})

.pipeline('request')
  .handleMessageStart(
    msg => (
      _target = _router.find(
        msg.head.headers.host,
        msg.head.path,
      ),
      _target && (__turnDown = true)
    )
  )
  .link(
    'forward', () => Boolean(_target),
    null
  )

.pipeline('forward')
  .muxHTTP(
    'connection',
    () => _target
  )

.pipeline('connection')
  .connect(
    () => _target
  )
