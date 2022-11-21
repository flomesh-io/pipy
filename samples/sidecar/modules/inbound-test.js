((
  config = pipy.solve('config.js'),
  targets = Object.values(config.Inbound.ClustersConfigs)[0],
  balancer = new algo.RoundRobinLoadBalancer(targets),

) => pipy({
  _target: null,
})

.pipeline()
.demuxHTTP().to(
  $=>$.muxHTTP(() => _target = balancer.next()).to(
    $=>$.connect(() => _target.id)
  )
)

)()
