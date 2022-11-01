//
// Basic routing test
//
// - /foo --> localhost:8080 --> "8080"
// - /bar --> localhost:8081 --> "8081"
// - /xyz --> localhost:8082 --> "8082XXX...(10000 Xs)..."
//

((
  router = new algo.URLRouter({
    '/foo': 'localhost:8080',
    '/bar': 'localhost:8081',
    '/xyz': 'localhost:8082',
  }),

) => pipy({
  _target: undefined,
})

  .listen(8000).link('test')
  .listen(8001).link('test').decodeHTTPResponse().replaceMessageBody(body => body.push('\n'))

  .listen(8080).serveHTTP(new Message('8080'))
  .listen(8081).serveHTTP(new Message('8081'))
  .listen(8082).serveHTTP(new Message('8082' + 'X'.repeat(10000)))

  .pipeline('test')
  .demuxHTTP().to(
    $=>$
    .handleMessageStart(
      msg => _target = router.find(msg.head.path)
    )
    .muxHTTP(() => _target).to(
      $=>$.connect(() => _target)
    )
  )

  .pipeline('http-to-lines')
  .decodeHTTPResponse()
  .replaceMessageBody(body => body.push('\n'))

)()
