(config =>

pipy({
  _logURL: new URL(config.logURL),
  _request: null,
  _requestTime: 0,
  _responseTime: 0,
})

.export('logger', {
  __logInfo: {},
})

.pipeline('request')
  .fork('log-request')

.pipeline('response')
  .fork('log-response')

.pipeline('log-request')
  .handleMessageStart(
    () => _requestTime = Date.now()
  )
  .decompressHTTP()
  .handleMessage(
    '256k',
    msg => _request = msg
  )

.pipeline('log-response')
  .handleMessageStart(
    () => _responseTime = Date.now()
  )
  .decompressHTTP()
  .replaceMessage(
    '256k',
    msg => (
      new Message(
        JSON.encode({
          req: {
            ..._request.head,
            body: _request.body.toString(),
          },
          res: {
            ...msg.head,
            body: msg.body.toString(),
          },
          reqTime: _requestTime,
          resTime: _responseTime,
          endTime: Date.now(),
          remoteAddr: __inbound.remoteAddress,
          remotePort: __inbound.remotePort,
          localAddr: __inbound.localAddress,
          localPort: __inbound.localPort,
          ...__logInfo,
        }).push('\n')
      )
    )
  )
  .merge('log-send', () => '')

.pipeline('log-send')
  .pack(
    1000,
    {
      timeout: 5,
    }
  )
  .replaceMessageStart(
    () => new MessageStart({
      method: 'POST',
      path: _logURL.path,
      headers: {
        'Host': _logURL.host,
        'Content-Type': 'application/json',
      }
    })
  )
  .encodeHTTPRequest()
  .connect(
    () => _logURL.host,
    {
      bufferLimit: '8m',
    }
  )

)(JSON.decode(pipy.load('config/logger.json')))
