((
  logger = new logging.JSONLogger('test').toHTTP('http://localhost:8123/log'),
  metric = new stats.Counter('test', ['path']),
  lb = new algo.RoundRobinLoadBalancer(
    new Array(12).fill().map((_, i) => `localhost:${8001+i}`)
  ),

) => pipy({
  _target: null
})

.listen(8000)
.demuxHTTP().to($=>$
  .handleMessageStart(
    ({ head }) => void (
      logger.log(head),
      metric.increase(),
      metric.withLabels(head.path).increase(),
      _target = lb.borrow()
    )
  )
  .muxHTTP().to($=>$
    .connect(() => _target.id)
  )
)

)()
