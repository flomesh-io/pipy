pipy({
  _router: new algo.URLRouter({
    '/hi/*': 'localhost:8080',
    '/echo': 'localhost:8081',
    '/ip/*': 'localhost:8082',
  }),

  _target: '',
})

.listen(8000)
  .demuxHTTP('request')

.pipeline('request')
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
  .muxHTTP('connection')

.pipeline('connection')
  .connect(
    () => _target
  )

.pipeline('404')
  .replaceMessage(
    new Message({ status: 404 }, 'No route')
  )
