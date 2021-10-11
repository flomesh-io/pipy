pipy({
  _router: new algo.URLRouter({
    '/*'   : '127.0.0.1:8080',
    '/hi/*': '127.0.0.1:8081',
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
  .muxHTTP(
    'connection',
    () => _target
  )

.pipeline('connection')
  .connect(
    () => _target
  )

.pipeline('404')
  .replaceMessage(
    new Message({ status: 404 }, 'No route')
  )
