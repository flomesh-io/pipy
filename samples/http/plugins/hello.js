((
  config = pipy.solve('config.js'),
  hello = config.hello,

) => pipy({
  _hi: null,
})

  .import({
    __route: 'main',
  })

  .pipeline()
  .branch(
    () => (_hi = hello[__route]), (
      $=>$
      .replaceData()
      .replaceMessage(
        () => new Message({
          status: _hi.status || 200,
          headers: {
            'content-type': _hi['content-type'] || 'text/plain',
          }
        }, _hi.message)
      )
    ),
    $=>$.chain()
  )

)()
