((
  config = pipy.solve('config.js'),
  routes = config.jwt,
  keys = new algo.Cache(
    kid => new crypto.PublicKey(
      pipy.load(`secret/${kid}.pem`)
    )
  ),

  INVALID_SIG = new Message({ status: 401 }, 'Invalid signature'),
  INVALID_KEY = new Message({ status: 401 }, 'Invalid key'),
  INVALID_TOKEN = new Message({ status: 401 }, 'Invalid token'),

) => pipy({
  _result: null,
})

  .import({
    __route: 'main'
  })

  .pipeline()
  .handleMessageStart(
    msg => _result = (
      ((r = routes[__route], header, token, jwt, kid) => (
        r ? (
          header = msg.head.headers.authorization || '',
          token = header.startsWith('Bearer ') ? header.substring(7) : header,
          jwt = new crypto.JWT(token),
          jwt.isValid ? (
            kid = jwt.header?.kid,
            r.keys?.includes?.(kid) ? (
              jwt.verify(keys.get(kid)) ? null : INVALID_SIG
            ) : (
              INVALID_KEY
            )
          ) : (
            INVALID_TOKEN
          )
        ) : null
      ))()
    )
  )
  .branch(
    () => Boolean(_result), (
      $=>$
      .replaceData()
      .replaceMessage(() => _result)
    ), (
      $=>$.chain()
    )
  )

)()
