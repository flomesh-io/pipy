(config =>

pipy({
  _router: new algo.URLRouter(
    Object.fromEntries(
      Object.entries(config.routes).map(
        ([k, v]) => [
          k,
          {
            ...v,
            rewrite: v.rewrite ? [
              new RegExp(v.rewrite[0]),
              v.rewrite[1],
            ] : undefined,
          }
        ]
      )
    )
  ),

  _route: null,
})

.export('router', {
  __serviceID: '',
})

.pipeline('request')
  .handleMessageStart(
    msg => (
      _route = _router.find(
        msg.head.headers.host,
        msg.head.path,
      ),
      _route && (
        __serviceID = _route.service,
        _route.rewrite && (
          msg.head.path = msg.head.path.replace(
            _route.rewrite[0],
            _route.rewrite[1],
          )
        )
      )
    )
  )

)(JSON.decode(pipy.load('config/router.json')))
