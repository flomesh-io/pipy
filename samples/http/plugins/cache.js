((
  config = pipy.solve('config.js'),
  caches = Object.fromEntries(
    Object.entries(config.cache || {}).map(
      ([k, v]) => [
        k, {
          cache: new algo.Cache(() => ({}), null, { ttl: v.timeout }),
          exts: v.extensions,
        }
      ]
    )
  ),

) => pipy({
  _cache: null,
  _key: undefined,
  _encoding: undefined,
  _found: undefined,
})

  .import({
    __route: 'main',
  })

  .pipeline()
  .handleMessageStart(
    msg => (
      ((
        path = msg.head.path,
        pathname = new URL(path).pathname,
      ) => (
        _encoding = msg.head.headers['accept-encoding'],
        _cache = caches[__route],
        _cache?.exts?.some?.(ext => pathname.endsWith(ext)) && (
          _key = path,
          _cache = _cache.cache,
          _found = _cache.get(_key)[_encoding]
        )
      ))()
    )
  )
  .branch(
    () => Boolean(_found), (
      $=>$
      .replaceData()
      .replaceMessage(() => _found)
    ),
    () => Boolean(_key), (
      $=>$
      .chain()
      .handleMessage(
        msg => _cache.get(_key)[_encoding] = msg
      )
    ), (
      $=>$
      .chain()
    )
  )

)()
