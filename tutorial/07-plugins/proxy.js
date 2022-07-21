((
  config = pipy.solve('config.js'),

) => pipy()

  .listen(config.listen)
  .demuxHTTP().to(
    $=>$.chain(config.plugins)
  )

)()
