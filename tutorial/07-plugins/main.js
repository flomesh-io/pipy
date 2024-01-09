((
  config = pipy.solve('config.js'),

) => pipy()

  .export('main', {
    __route: undefined,
  })

  .listen(config.listen)
  .demuxHTTP().to(
    $=>$.chain(config.plugins)
  )

)()
