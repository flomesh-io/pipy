((
  allMethods = ['GET', 'HEAD', 'POST', 'PUT', 'DELETE', 'PATCH'],

  makeServiceHandler = (portConfig, service) => (
    (
      rules = portConfig.HttpServiceRouteRules[service]?.RouteRules || {},
      tree = {},
    ) => (
      Object.entries(rules).forEach(
        ([path, config]) => (
          (
            pathPattern = new RegExp(path),
            headerRules = config.Headers ? Object.entries(config.Headers).map(([k, v]) => [k, new RegExp(v)]) : null,
            rule = headerRules ? (
              (path, headers) => pathPattern.test(path) && headerRules.every(([k, v]) => v.test(headers[k] || '')) && (__route = config)
            ) : (
              (path) => pathPattern.test(path) && (__route = config)
            ),
            allowedIdentities = config.AllowedServices ? new Set(config.AllowedServices) : [''],
            allowedMethods = config.Methods || allMethods,
          ) => (
            allowedIdentities.forEach(
              identity => (
                (
                  methods = tree[identity] || (tree[identity] = {}),
                ) => (
                  allowedMethods.forEach(
                    method => (methods[method] || (methods[method] = [])).push(rule)
                  )
                )
              )()
            )
          )
        )()
      ),

      (method, path, headers) => void (
        tree[headers.serviceidentity || '']?.[method]?.find?.(rule => rule(path, headers))
      )
    )
  )(),

  makePortHandler = portConfig => (
    (
      ingressRanges = Object.keys(portConfig.SourceIPRanges || {}).map(k => new Netmask(k)),

      serviceHandlers = new algo.Cache(
        service => makeServiceHandler(portConfig, service)
      ),

      makeHostHandler = (portConfig, host) => (
        serviceHandlers.get(portConfig.HttpHostPort2Service[host])
      ),

      hostHandlers = new algo.Cache(
        host => makeHostHandler(portConfig, host)
      ),

    ) => (
      ingressRanges.length > 0 ? (
        msg => void(
          (
            ip = __inbound.remoteAddress || '127.0.0.1',
            ingressRange = ingressRanges.find(r => r.contains(ip)),
            head = msg.head,
            headers = head.headers,
            handler = hostHandlers.get(ingressRange ? '*' : headers.host),
          ) => (
            handler(head.method, head.path, headers)
          )
        )()
      ) : (
        msg => void (
          (
            head = msg.head,
            headers = head.headers,
            handler = hostHandlers.get(headers.host),
          ) => (
            handler(head.method, head.path, headers)
          )
        )()
      )
    )
  )(),

  portHandlers = new algo.Cache(makePortHandler),

) => pipy()

.import({
  __port: 'inbound-main',
})

.export('inbound-http-routing', {
  __route: null,
})

.pipeline()
.demuxHTTP().to(
  $=>$
  .handleMessageStart(
    msg => portHandlers.get(__port)(msg)
  )
  .chain()
)

)()
