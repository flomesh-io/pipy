((
  config = pipy.solve('config.js'),

  makeTargetBalancer = (clusterName) => (
    (
      targets = config.Outbound.ClustersConfigs[clusterName].Endpoints
    ) => (
      new algo.RoundRobinLoadBalancer(targets)
    )
  )(),

  targetBalancers = new algo.Cache(makeTargetBalancer),

  makeClusterBalancer = (clusters) => (
    (
      balancer = new algo.RoundRobinLoadBalancer(clusters)
    ) => (
      () => targetBalancers.get(balancer.next()?.id).next()
    )
  )(),

  clusterBalancers = new algo.Cache(makeClusterBalancer),

) => pipy({
  _target: null,
})

.import({
  __route: 'outbound-http-routing',
})

.pipeline()
.branch(
  () => (_target = clusterBalancers.get(__route?.TargetClusters)()), (
    $=>$.muxHTTP(() => _target).to(
      $=>$.connect(() => _target.id)
    )
  ), (
    $=>$.chain()
  )
)

)()
