(config =>

pipy({
  _keys: (
    Object.fromEntries(
      Object.entries(config.keys).map(
        ([k, v]) => [
          k,
          new crypto.PrivateKey(v.pem)
        ]
      )
    )
  ),

  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => [
          k,
          {
            keys: v.keys ? Object.fromEntries(v.keys.map(k => [k, true])) : undefined,
          }
        ]
      )
    )
  ),
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'proxy',
})

.pipeline('verify')
  .replaceMessage(
    msg => (
      ((
        service,
        header,
        jwt,
        kid,
        key,
      ) => (
        service = _services[__serviceID],
        service?.keys ? (
          header = msg.head.headers.authorization || '',
          header.startsWith('Bearer ') && (header = header.substring(7)),
          jwt = new crypto.JWT(header),
          jwt.isValid ? (
            kid = jwt.header?.kid,
            key = _keys[kid],
            key ? (
              service.keys[kid] ? (
                jwt.verify(key) ? (
                  msg
                ) : (
                  __turnDown = true,
                  new Message({ status: 401 }, 'Invalid signature')
                )
              ) : (
                __turnDown = true,
                new Message({ status: 403 }, 'Access denied')
              )
            ) : (
              __turnDown = true,
              new Message({ status: 401 }, 'Invalid key')
            )
          ) : (
            __turnDown = true,
            new Message({ status: 401 }, 'Invalid token')
          )
        ) : msg
      ))()
    )
  )

)(JSON.decode(pipy.load('jwt.json')))