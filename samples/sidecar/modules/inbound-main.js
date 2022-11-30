((
  config = pipy.solve('config.js'),

  makePortHandler = port => (
    (
      portConfig = config.Inbound.TrafficMatches[port || 0],
      protocol = portConfig?.Protocol === 'http' || portConfig?.Protocol === 'grpc' ? 'http' : 'tcp',
      isHTTP2 = portConfig?.Protocol === 'grpc',
      allowedEndpointsLocal = portConfig?.AllowedEndpoints,
      allowedEndpointsGlobal = config.AllowedEndpoints,
      allowedEndpoints = new Set(
        Object.keys(allowedEndpointsLocal || allowedEndpointsGlobal || {}).filter(
          k => (
            (!allowedEndpointsLocal || k in allowedEndpointsLocal) &&
            (!allowedEndpointsGlobal || k in allowedEndpointsGlobal)
          )
        )
      ),

    ) => (
      !portConfig && (
        () => undefined
      ) ||

      allowedEndpoints.size > 0 && (
        () => void (
          allowedEndpoints.has(__inbound.remoteAddress || '127.0.0.1') && (
            __port = portConfig,
            __protocol = protocol,
            __isHTTP2 = isHTTP2
          )
        )
      ) ||

      (
        () => void (
          __port = portConfig,
          __protocol = protocol,
          __isHTTP2 = isHTTP2
        )
      )

    )
  )(),

  portHandlers = new algo.Cache(makePortHandler),

) => pipy()

.export('inbound-main', {
  __port: null,
  __protocol: undefined,
  __isHTTP2: false,
})

.pipeline()
.onStart(
  () => void portHandlers.get(__inbound.destinationPort)()
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
