((
  config = pipy.solve('config.js'),

  makeBalancer = (clusters) => (
    (
      clusterName = Object.keys(clusters || {})[0],
      targets = config.Inbound.ClustersConfigs[clusterName] || [],
    ) => (
      new algo.RoundRobinLoadBalancer(targets)
    )
  )(),

  balancers = new algo.Cache(makeBalancer),

) => pipy({
  _target: null,
})

.import({
  __route: 'inbound-http-routing',
})

.pipeline()
.branch(
  () => (_target = balancers.get(__route?.TargetClusters).next()), (
    $=>$.muxHTTP(() => _target).to(
      $=>$.connect(() => _target.id)
    )
  ), (
    $=>$.chain()
  )
)

)()
