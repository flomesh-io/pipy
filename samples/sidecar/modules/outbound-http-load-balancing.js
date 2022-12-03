((
  config = pipy.solve('config.js'),

  tlsCertChain = config?.Certificate?.CertChain,
  tlsPrivateKey = config?.Certificate?.PrivateKey,
  tlsIssuingCA = config?.Certificate?.IssuingCA,

  makeClusterConfig = (clusterName) => (
    (
      clusterConfig = config?.Outbound?.ClustersConfigs?.[clusterName],
    ) => (
      {
        targetBalancer: new algo.RoundRobinLoadBalancer(clusterConfig?.Endpoints),
        needRetry: Boolean(clusterConfig?.RetryPolicy?.NumRetries),
        numRetries: clusterConfig?.RetryPolicy?.NumRetries,
        lowerbound: clusterConfig?.RetryPolicy?.RetryOn ? clusterConfig.RetryPolicy.RetryOn.replaceAll('x', '0') : 500,
        upperbound: clusterConfig?.RetryPolicy?.RetryOn ? clusterConfig.RetryPolicy.RetryOn.replaceAll('x', '9') : 599,
        retryBackoffBaseInterval: clusterConfig?.RetryPolicy?.RetryBackoffBaseInterval || 1, // default 1 second
        muxHttpOptions: {
          version: () => __isHTTP2 ? 2 : 1,
          maxMessages: clusterConfig?.ConnectionSettings?.http?.MaxRequestsPerConnection
        }
      }
    )
  )(),

  clusterConfigs = new algo.Cache(makeClusterConfig),

  makeClusterBalancer = (clusters) => (
    (
      balancer = new algo.RoundRobinLoadBalancer(clusters)
    ) => (
      () => (
        clusterConfigs.get(balancer.next()?.id)
      )
    )
  )(),

  clusterBalancers = new algo.Cache(makeClusterBalancer),

  calcScaleRatio = (n) => (
    n < 1 ? 0 : (n = Math.pow(2, n - 1), n > 10 ? 10 : n)
  ),

  shouldRetry = (statusCode) => (
    (_clusterConfig.lowerbound <= _statusCode && statusCode <= _clusterConfig.upperbound) &&
    (_retryCount++ < _clusterConfig.numRetries)
  ),

) => pipy({
  _target: null,
  _retryCount: null,
  _clusterConfig: null,
})

.import({
  __isHTTP2: 'outbound-main',
  __route: 'outbound-http-routing',
})

.pipeline()
.onStart(
  () => void (
    (_clusterConfig = clusterBalancers.get(__route?.TargetClusters)()) && (
      _target = _clusterConfig.targetBalancer?.next?.()
    )
  )
)
.branch(
  () => !_target, $=>$.chain(),
  () => _clusterConfig.needRetry, (
    $=>$
    .handleMessageStart(
      () => (
        _retryCount = 0
      )
    )
    .replay({ 'delay': () => _clusterConfig.retryBackoffBaseInterval * calcScaleRatio(_retryCount) }).to(
      $=>$
      .link('upstream')
      .replaceMessageStart(
        evt => (
          shouldRetry(evt.head.status) ? new StreamEnd('Replay') : evt
        )
      )
    )
  ), (
    $=>$.link('upstream')
  )
)

.pipeline('upstream')
.muxHTTP(() => _target, () => _clusterConfig.muxHttpOptions).to(
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

)()
