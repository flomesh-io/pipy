((
  config = pipy.solve('config.js'),

  makePortHandler = port => (
    (
      portConfig = config.Inbound.TrafficMatches[port || 0],
      protocol = portConfig?.Protocol === 'http' || portConfig?.Protocol === 'grpc' ? 'http' : 'tcp',
      allowedEndpointsLocal = portConfig?.AllowedEndpoints || {},
      allowedEndpointsGlobal = config.AllowedEndpoints || {},
      allowedEndpoints = new Set,

    ) => (
      Object.keys(allowedEndpointsLocal).forEach(k => (k in allowedEndpointsGlobal) && allowedEndpoints.add(k)),
      Object.keys(allowedEndpointsGlobal).forEach(k => (k in allowedEndpointsLocal) && allowedEndpoints.add(k)),

      !portConfig && (
        () => undefined
      ) ||

      allowedEndpoints.size > 0 && (
        () => void (
          allowedEndpoints.has(__inbound.remoteAddress || '127.0.0.1') && (
            __port = portConfig,
            __protocol = protocol
          )
        )
      ) ||

      (
        () => void (
          __port = portConfig,
          __protocol = protocol
        )
      )

    )
  )(),

  portHandlers = new algo.Cache(makePortHandler),

) => pipy()

.export('inbound-classification', {
  __port: null,
  __protocol: undefined,
})

.pipeline()
.onStart(
  () => portHandlers.get(__inbound.destinationPort)()
)
.branch(
  () => __protocol === 'http', (
    $=>$
    .replaceStreamStart()
    .chain([
      'modules/inbound-tls-termination.js',
      'modules/inbound-http-routing.js',
      'modules/inbound-http-load-balancing.js',
      'modules/inbound-http-default.js',
    ])
  ),

  () => __protocol == 'tcp', (
    $=>$.chain([
      'modules/inbound-tls-termination.js',
      'modules/inbound-tcp-load-balancing.js',
      'modules/inbound-tcp-default.js',
    ])

  ), (
    $=>$.replaceStreamStart(
      new StreamEnd('ConnectionReset')
    )
  )
)

)()
