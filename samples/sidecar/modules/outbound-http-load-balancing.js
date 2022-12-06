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
        retryStatusCodes: (clusterConfig?.RetryPolicy?.RetryOn || '5xx').split(',').reduce(
          (lut, code) => (
            code.endsWith('xx') ? (
              new Array(100).fill(0).forEach((_, i) => lut[(code.charAt(0)|0)*100+i] = true)
            ) : (
              lut[code|0] = true
            ),
            lut
          ),
          []
        ),
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

  shouldRetry = (statusCode) => (
    _clusterConfig.retryStatusCodes[statusCode] ? (
      (_retryCount < _clusterConfig.numRetries) ? (
        _retryCount++,
        true
      ) : (
        false
      )
    ) : (
      false
    )
),

) => pipy({
  _target: null,
  _retryCount: 0,
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
    .replay({
        delay: () => _clusterConfig.retryBackoffBaseInterval * Math.min(10, Math.pow(2, _retryCount-1)|0)
    }).to(
      $=>$
      .link('upstream')
      .replaceMessageStart(
        msg => (
          shouldRetry(msg.head.status) ? new StreamEnd('Replay') : msg
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
