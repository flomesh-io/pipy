(config =>

pipy({
  _router: new algo.URLRouter(config.routes),
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
    ''
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

)(JSON.decode(pipy.load('config/router.json')))
