((
  config = pipy.solve('config.js'),

  makePortHandler = (port) => (
    (
      destinations = (config.Outbound.TrafficMatches[port] || []).map(
        config => ({
          ranges: Object.entries(config.DestinationIPRanges).map(
            ([k, config]) => ({ mask: new Netmask(k), config })
          ),
          config,
        })
      ),

      destinationHandlers = new algo.Cache(
        (address) => (
          (
            dst = destinations.find(
              dst => dst.ranges.find(
                r => r.mask.contains(address)
              )
            ),
            protocol = dst?.Protocol === 'http' || dst?.Protocol === 'grpc' ? 'http' : 'tcp',
            isHTTP2 = dst?.Protocol === 'grpc',
          ) => (
            () => (
              __port = dst?.config,
              __protocol = protocol,
              __isHTTP2 = isHTTP2
            )
          )
        )()
      ),

    ) => (
      () => (
        destinationHandlers.get(__inbound.destinationAddress || '127.0.0.1')()
      )
    )
  )(),

  portHandlers = new algo.Cache(makePortHandler),

) => pipy()

.export('outbound-main', {
  __port: null,
  __protocol: undefined,
  __isHTTP2: false,
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
      'modules/outbound-http-routing.js',
      'modules/outbound-http-load-balancing.js',
      'modules/outbound-http-default.js',
    ])
  ),

  () => __protocol === 'tcp', (
    $=>$.chain([
      'modules/outbound-tcp-load-balancing.js',
      'modules/outbound-tcp-default.js',
    ])

  ), (
    $=>$.replaceStreamStart(
      new StreamEnd('ConnectionReset')
    )
  )
)

)()
