((
  allMethods = ['GET', 'HEAD', 'POST', 'PUT', 'PATCH', 'DELETE'],

  makeServiceHandler = (portConfig, service) => (
    (
      rules = portConfig.HttpServiceRouteRules[service],
      tree = {},
    ) => (
      Object.entries(rules).forEach(
        ([path, config]) => (
          (
            pathPattern = new RegExp(path),
            headerRules = Object.entries(config.Headers || {}).map(([k, v]) => [k, new RegExp(v)]),
            rule = headerRules.length > 0 ? (
              (path) => pathPattern.test(path) && (__route = config)
            ) : (
              (path, headers) => pathPattern.test(path) && (headerRules.every(([k, v]) => v.test(headers[k] || ''))) && (__route = config)
            ),
            allowedMethods = config.Methods || allMethods,
          ) => (
            allowedMethods.forEach(
              method => (tree[method] || (tree[method] = [])).push(rule)
            )
          )
        )()
      ),

      (method, path, headers) => void (
        tree[method]?.find?.(rule => rule(path, headers))
      )
    )
  )(),

  makePortHandler = (portConfig) => (
    (
      serviceHandlers = new algo.Cache(
        (service) => makeServiceHandler(portConfig, service)
      ),

      hostHandlers = new algo.Cache(
        (host) => serviceHandlers.get(portConfig.HttpHostPort2Service[host])
      ),

    ) => (
      (msg) => (
        (
          head = msg.head,
          headers = head.headers,
        ) => (
          hostHandlers.get(headers.host)(head.method, head.path, headers)
        )
      )()
    )
  )(),

  portHandlers = new algo.Cache(makePortHandler),

) => pipy()

.import({
  __port: 'outbound-main',
})

.export('outbound-http-routing', {
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
