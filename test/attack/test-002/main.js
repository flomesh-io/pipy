//
// Basic routing test with throttling
//
// - /foo -----------------> localhost:8080 --> "8080"
// - /bar --> (throttle) --> localhost:8081 --> "8081"
// - /xyz -----------------> localhost:8082 --> (throttle) --> "8082XXX...(10000 Xs)..."
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

.listen(8080).serveHTTP(new Message('8080'))
.listen(8081).serveHTTP(new Message('8081'))
.listen(8082).serveHTTP(new Message('8082' + 'X'.repeat(10000)))
             .throttleDataRate(() => new algo.Quota(100000, { per: '1s' }))

.listen(8000)
.demuxHTTP().to(
  $=>$
  .handleMessageStart(
    msg => _target = router.find(msg.head.path)
  )
  .branch(
    () => _target === 'localhost:8081', (
      $=>$.throttleMessageRate(new algo.Quota(10, { per: '1s' }))
    ), (
      $=>$
    )
  )
  .muxHTTP(() => _target).to(
    $=>$.connect(() => _target)
  )
)

)()
