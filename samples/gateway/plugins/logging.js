import config from '/config.js'

var logger = config.log.reduce(
  (logger, target) => (
    logger.toHTTP(
      target.url, {
        headers: target.headers,
        batch: target.batch,
      }
    )
  ),
  new logging.JSONLogger('http')
)

var $ctx
var $reqHead
var $reqTime
var $reqSize
var $resHead
var $resTime
var $resSize

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .handleMessageStart(
    function (msg) {
      $reqHead = msg.head
      $reqTime = Date.now()
    }
  )
  .handleData(
    function (data) {
      $reqSize += data.size
    }
  )
  .pipeNext()
  .handleMessageStart(
    function (msg) {
      $resHead = msg.head
      $resTime = Date.now()
    }
  )
  .handleData(
    function (data) {
      $resSize += data.size
    }
  )
  .handleMessageEnd(
    function () {
      var ib = $ctx.inbound
      logger.log({
        remoteAddr: ib.remoteAddress,
        remotePort: ib.remotePort,
        req: $reqHead,
        res: $resHead,
        reqSize: $reqSize,
        resSize: $resSize,
        reqTime: $reqTime,
        resTime: $resTime,
        endTime: Date.now(),
      })
    }
  )
)
