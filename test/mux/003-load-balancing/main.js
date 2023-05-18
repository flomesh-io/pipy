//
// Basic load-balancing test with throttling
//
// - /foo -----------------> localhost:8080 --> "8080"
// - /bar --> (throttle) --> localhost:8081 --> "8081"
// - /xyz -----------------> localhost:8082 --> (throttle) --> "8082XXX...(10000 Xs)..."
//

((
  router = new algo.URLRouter({
    '/foo': new algo.RoundRobinLoadBalancer({ 'localhost:8080': 2, 'localhost:8081': 1, 'localhost:8082': 1 }),
    '/bar': new algo.RoundRobinLoadBalancer({ 'localhost:8088': 2, 'localhost:8089': 8 }),
  }),

) => pipy({
  _quota: null,
  _target: null,
})

.listen(8080).onStart(() => void(_quota = new algo.Quota(1, { per: '0.1s' }))).link('service')
.listen(8081).onStart(() => void(_quota = new algo.Quota(2, { per: '0.5s' }))).link('service')
.listen(8082).onStart(() => void(_quota = new algo.Quota(1, { per: '0.2s' }))).link('service')
.listen(8088).onStart(() => void(_quota = new algo.Quota(1, { per: '0.1s' }))).link('service')
.listen(8089).onStart(() => void(_quota = new algo.Quota(5, { per: '0.2s' }))).link('service')

.listen(8000)
.demuxHTTP().to(
  $=>$
  .handleMessageStart(
    msg => _target = router.find(msg.head.path).next({})
  )
  .muxHTTP(() => _target).to(
    $=>$.connect(() => _target.id)
  )
)

.pipeline('service')
.demuxHTTP().to(
  $=>$
  .throttleMessageRate(() => _quota)
  .replaceMessage(() => new Message(__inbound.localPort.toString()))
)

)()
