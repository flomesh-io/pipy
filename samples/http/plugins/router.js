((
  config = pipy.solve('config.js'),
  router = new algo.URLRouter(
    Object.fromEntries(
      Object.entries(config.endpoints).map(
        ([k, { route, rewrite }]) => [
          k, { route, rewrite: rewrite && [new RegExp(rewrite[0]), rewrite[1]] }
        ]
      )
    )
  ),

) => pipy()

  .import({
    __route: 'main',
  })

  .pipeline()
  .handleMessageStart(
    msg => (
      ((
        r = router.find(
          msg.head.headers.host,
          msg.head.path,
        )
      ) => (
        __route = r?.route,
        r?.rewrite && (
          msg.head.path = msg.head.path.replace(r.rewrite[0], r.rewrite[1])
        )
      ))()
    )
  )
  .chain()

)()
