((
  config = pipy.solve('config.js'),

  tlsCertChain = config?.Certificate?.CertChain,
  tlsPrivateKey = config?.Certificate?.PrivateKey,
  tlsIssuingCA = config?.Certificate?.IssuingCA,

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
    $=>$
    .muxHTTP(() => _target).to(
      $=>$
      .connectTLS({
        certificate: () => ({
          cert: new crypto.Certificate(tlsCertChain),
          key: new crypto.PrivateKey(tlsPrivateKey),
        }),
        trusted: tlsIssuingCA ? [new crypto.Certificate(tlsIssuingCA)] : [],
      }).to(
        $=>$.connect(() => _target.id)
      )
    )

  ), (
    $=>$.chain()
  )
)

)()
