pipy({
  _balancer: new algo.RoundRobinLoadBalancer({
    '127.0.0.1:8080': 2,
    '127.0.0.1:8081': 1,
    '127.0.0.1:8082': 1,
  }),
  _target: undefined,
})

// TCP inbound
.listen(6080)
  .onSessionStart(
    () => _target = _balancer.select()
  )
  .onSessionEnd(
    () => _balancer.deselect?.(_target)
  )
  .connect(
    () => _target
  )

// Mock service on port 8080
.listen(8080)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello from service 8080')
  )
  .encodeHTTPResponse()

// Mock service on port 8081
.listen(8081)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello from service 8081')
  )
  .encodeHTTPResponse({ status: 300 })

// Mock service on port 8082
.listen(8082)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello from service 8082')
  )
  .encodeHTTPResponse({ status: 500 })