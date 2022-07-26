((
  config = pipy.solve('config.js'),
  router = new algo.URLRouter(config.routes),

) => pipy()

  .import({
    __route: 'main',
  })

  .pipeline()
  .handleMessageStart(
    msg => (
      __route = router.find(
        msg.head.headers.host,
        msg.head.path,
      )
    )
  )
  .chain()

)()
