((
  config = pipy.solve('config.js'),
  logger = config.log.reduce(
    (logger, target) => (
      logger.toHTTP(
        target.url, {
          headers: target.headers,
          batch: target.batch
        }
      )
    ),
    new logging.JSONLogger('http')
  ),

) => pipy({
  _reqHead: null,
  _reqTime: 0,
  _reqSize: 0,
  _resHead: null,
  _resTime: 0,
  _resSize: 0,
})

  .pipeline()
  .handleMessageStart(
    msg => (
      _reqHead = msg.head,
      _reqTime = Date.now()
    )
  )
  .handleData(data => _reqSize += data.size)
  .chain()
  .handleMessageStart(
    msg => (
      _resHead = msg.head,
      _resTime = Date.now()
    )
  )
  .handleData(data => _resSize += data.size)
  .handleMessageEnd(
    () => (
      logger.log({
        req: _reqHead,
        res: _resHead,
        reqSize: _reqSize,
        resSize: _resSize,
        reqTime: _reqTime,
        resTime: _resTime,
        endTime: Date.now(),
      })
    )
  )

)()
