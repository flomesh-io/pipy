((
  config = pipy.solve('config.js'),
  router = new algo.URLRouter(config.routes),

) => pipy()

  .export('router', {
    __serviceID: undefined,
  })

  .pipeline()
  .handleMessageStart(
    msg => (
      __serviceID = router.find(
        msg.head.headers.host,
        msg.head.path,
      )
    )
  )
  .chain()

)()
