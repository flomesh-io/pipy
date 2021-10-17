(config =>

pipy({
  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => [
          k,
          {
            white: (
              v.white?.length > 0 ? (
                Object.fromEntries(
                  v.white.map(
                    ip => [ip, true]
                  )
                )
              ) : null
            ),
            black: (
              v.black?.length > 0 ? (
                Object.fromEntries(
                  v.black.map(
                    ip => [ip, true]
                  )
                )
              ) : null
            ),
          }
        ]
      )
    )
  ),

  _service: null,
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'router',
})

.pipeline('request')
  .handleSessionStart(
    () => (
      _service = _services[__serviceID],
      __turnDown = Boolean(
        _service && (
          _service.white ? (
            !_service.white[__inbound.remoteAddress]
          ) : (
            _service.black?.[__inbound.remoteAddress]
          )
        )
      )
    )
  )
  .link(
    'deny', () => __turnDown,
    'bypass'
  )

.pipeline('deny')
  .replaceMessage(
    new Message({ status: 403 }, 'Access denied')
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('config/ban.json')))
