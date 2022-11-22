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
  __port: 'inbound-main',
})

.pipeline()
.branch(
  () => (_target = balancers.get(__port?.TargetClusters).next()), (
    $=>$.connect(() => _target.id)
  ), (
    $=>$.chain()
  )
)

)()
