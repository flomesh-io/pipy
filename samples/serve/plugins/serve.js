((
  WWW_ROOT = '/www',

) =>

pipy({
  _file: null,
})

  .pipeline()
  .handleMessageStart(
    msg => (
      _file = http.File.from(WWW_ROOT + new URL(msg.head.path).pathname)
    )
  )
  .branch(
    () => Boolean(_file), (
      $=>$
      .replaceMessage(
        msg => _file.toMessage(msg.head.headers['accept-encoding'])
      )
    ),
    $=>$.chain()
  )

)()
