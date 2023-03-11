((
  cache = new algo.Cache(() => ({})),

) => pipy({
  _key: undefined,
  _encoding: undefined,
  _found: undefined,
})

  .pipeline()
  .handleMessageStart(
    msg => (
      _key = msg.head.path,
      _encoding = msg.head.headers['accept-encoding'],
      _found = cache.find(_key)?.[_encoding]
    )
  )
  .branch(
    () => _found, (
      $=>$
      .replaceMessage(() => _found)
    ), (
      $=>$
      .chain()
      .handleMessage(
        msg => (
          (
            status = msg.head.status || 200,
          ) => (
            200 <= status && status < 400 && (
              cache.get(_key)[_encoding] = msg
            )
          )
        )()
      )
    )
  )

)()
