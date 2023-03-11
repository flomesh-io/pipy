((
  config = pipy.solve('config.js'),

) =>

pipy({
  _method: '',
})

  .listen(config.port || 80)
  .demuxHTTP().to(
    $=>$
    .handleMessageStart(
      msg => _method = msg.head.method
    )
    .branch(
      () => _method === 'GET' || _method === 'HEAD', (
        $=>$
        .chain([
          'plugins/cache.js',
          'plugins/serve.js',
          'plugins/default.js',
        ])
      ), (
        $=>$
        .replaceData()
        .replaceMessage(
          new Message({ status: 405 }, 'Method Not Allowed')
        )
      )
    )
  )

)()
