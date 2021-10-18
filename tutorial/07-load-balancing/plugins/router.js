(config =>

pipy({
  _router: new algo.URLRouter(config.routes),
})

.export('router', {
  __serviceID: '',
})

.pipeline('request')
  .handleMessageStart(
    msg => (
      __serviceID = _router.find(
        msg.head.headers.host,
        msg.head.path,
      )
    )
  )

)(JSON.decode(pipy.load('config/router.json')))
