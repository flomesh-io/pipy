((
  metricRequestCount = new stats.Counter('request_count', ['route']),
  metricResponseStatus = new stats.Counter('response_status', ['route', 'status']),
  metricResponseLatency = new stats.Histogram('request_latency', ['route'],
    new Array(26).fill().map((_,i) => Math.pow(1.5, i+1)|0).concat([Infinity])
  ),

) => pipy({
  _requestTime: 0,
})

  .import({
    __route: 'main',
  })

  .pipeline()
  .handleMessageStart(
    () => (
      _requestTime = Date.now(),
      metricRequestCount.withLabels(__route).increase()
    )
  )
  .chain()
  .handleMessageStart(
    msg => (
      metricResponseLatency.withLabels(__route).observe(Date.now() - _requestTime),
      metricResponseStatus.withLabels(__route, msg.head.status).increase()
    )
  )
)()
