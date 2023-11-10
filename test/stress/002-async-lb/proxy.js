((
  logger = new logging.JSONLogger('test').toHTTP('http://localhost:8123/log'),
  metric = new stats.Counter('test', ['path']),
  lb = new algo.RoundRobinLoadBalancer(
    new Array(12).fill().map((_, i) => `localhost:${8001+i}`)
  ),

) => pipy({
  _resolveLB: null,
  _target: null,
})

.listen(8000)
.demuxHTTP().to($=>$
  .handleMessageStart(
    ({ head }) => void (
      logger.log(head),
      metric.increase(),
      metric.withLabels(head.path).increase()
    )
  )
  .fork().to($=>$
    .replaceMessageStart(new MessageStart)
    .linkAsync(() => 'lb')
    .handleMessageStart(
      msg => _resolveLB(msg.head.target)
    )
  )
  .wait(() => new Promise(r => _resolveLB = r).then(t => _target = t))
  .muxHTTP().to($=>$
    .connect(() => _target)
  )
)

.branch(
  __thread.id === 0, ($=>$
    .pipeline('lb')
    .replaceMessageStart(
      () => new MessageStart({ target: lb.select() })
    )
  )
)

)()
