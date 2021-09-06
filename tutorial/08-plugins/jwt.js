pipy({
  _keys: {
    'key-1': new crypto.PrivateKey(os.readFile('sample-key-rsa.pem')),
    'key-2': new crypto.PrivateKey(os.readFile('sample-key-ecdsa.pem')),
  },

  _services: {
    'service-2': {
      keys: { 'key-1': true, 'key-2': true },
    },
  },
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