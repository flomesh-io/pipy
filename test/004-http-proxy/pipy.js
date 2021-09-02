pipy({
  _router: new algo.URLRouter({
    '/hello': 8080,
    '/hi/*': 8081,
    'foo.com/bar/*': 8080,
    '*.foo.com/bar': 8081,
    '*.foo.org/foo/bar/*': 8080,
  }),
  _target: null,
})

// HTTP inbound
.listen(6080)
  .fork('routing')
  .wait(
    () => _target !== null
  )
  .link(
    'pass', () => _target,
    '404'
  )

// Find route for host + path
.pipeline('routing')
  .decodeHTTPRequest()
  .onMessageStart(
    evt => _target = _router.find(
      evt.head.headers.host,
      evt.head.path,
    )
  )

// Pass to upstream
.pipeline('pass')
  .connect(
    () => '127.0.0.1:' + _target
  )

// Not found
.pipeline('404')
  .decodeHTTPRequest()
  .replaceMessage(
    new Message({ status: 404 }, 'Not found\n')
  )
  .encodeHTTPResponse()

// Mock service on port 8080
.listen(8080)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello from service 8080\n')
  )
  .encodeHTTPResponse()

// Mock service on port 8081
.listen(8081)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello from service 8081\n')
  )
  .encodeHTTPResponse()