((
  config = pipy.solve('config.js'),
  certChain = config?.Certificate?.CertChain,
  issuingCA = config?.Certificate?.IssuingCA,

  makeBalancer = (clusters) => (
    (
      clusterName = Object.keys(clusters || {})[0],
      targets = config?.Outbound?.ClustersConfigs?.[clusterName]?.Endpoints || [],
    ) => (
      new algo.RoundRobinLoadBalancer(targets)
    )
  )(),

  balancers = new algo.Cache(makeBalancer),

) => pipy({
  _target: null,
})

.import({
  __port: 'outbound-main',
})

.pipeline()
.branch(
  () => (_target = balancers.get(__port?.TargetClusters).next()), (
    $=>$
    .branch(
      () => Boolean(certChain), (
        $=>$.connectTLS({
          certificate: {
            cert: new crypto.Certificate(certChain),
            key: new crypto.PrivateKey(config?.Certificate?.PrivateKey),
          },
          trusted: issuingCA ? [new crypto.Certificate(issuingCA)] : [],
        }).to(
          $=>$.connect(() => _target.id)
        )
      ), (
        $=>$.connect(() => _target.id)
      )
    )

  ), (
    $=>$.chain()
  )
)

)()
