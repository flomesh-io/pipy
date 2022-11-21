((
  config = pipy.solve('config.js'),
  sites = config['serve-files'],

) => pipy({
  _file: null,
})

  .import({
    __route: 'main',
  })

  .pipeline()
  .handleMessageStart(
    msg => (
      ((
        root = sites[__route]?.root,
      ) => (
        root && (
          _file = http.File.from(root + new URL(msg.head.path).pathname)
        )
      ))()
    )
  )
  .branch(
    () => Boolean(_file), (
      $=>$
      .replaceData()
      .replaceMessage(
        msg => _file.toMessage(msg.head.headers['accept-encoding'])
      )
    ),
    $=>$.chain()
  )

)()
