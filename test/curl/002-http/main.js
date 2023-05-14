((
  router = new algo.URLRouter({
    '/foo': new algo.RoundRobinLoadBalancer([
      'localhost:8080',
      'localhost:8081',
      'localhost:8082',
    ]),
    '/bar': new algo.RoundRobinLoadBalancer([
      'localhost:8080',
    ]),
  })
) =>

pipy({
  _target: null,
})

.repeat(
  [8080, 8081, 8082], ($, port) => ($
    .listen(port)
    .serveHTTP(() => new Message(`${port}\n`))
  )
)

.listen(8000)
.demuxHTTP().to(
  $=>$
  .branchMessageStart(
    msg => _target = router.find(msg.head.path)?.borrow?.(), (
      $=>$.muxHTTP(() => _target).to(
        $=>$.connect(() => _target.id)
      )
    ), (
      $=>$.replaceMessage(
        new Message({ status: 404 }, 'not found\n')
      )
    )
  )
)

)()
