var metricRequestCount = new stats.Counter('request_count', ['route'])
var metricResponseStatus = new stats.Counter('response_status', ['route', 'status'])
var metricResponseLatency = new stats.Histogram('request_latency',
  new Array(26).fill().map((_,i) => Math.pow(1.5, i+1)|0).concat([Infinity]),
  ['route'],
)

var $ctx
var $requestTime

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .handleMessageStart(
    function () {
      $requestTime = Date.now()
      metricRequestCount.withLabels($ctx.route).increase()
    }
  )
  .pipeNext()
  .handleMessageStart(
    function (msg) {
      var r = $ctx.route
      metricResponseLatency.withLabels(r).observe(Date.now() - $requestTime)
      metricResponseStatus.withLabels(r, msg.head.status || 200).increase()
    }
  )
)
