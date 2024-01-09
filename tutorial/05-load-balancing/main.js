((
  router = new algo.URLRouter({
    '/hi/*': new algo.RoundRobinLoadBalancer(['localhost:8080', 'localhost:8082']),
    '/echo': new algo.RoundRobinLoadBalancer(['localhost:8081']),
    '/ip/*': new algo.RoundRobinLoadBalancer(['localhost:8082']),
  }),

) => pipy({
  _target: undefined,
})

  .listen(8000)
  .demuxHTTP().to(
    $=>$
    .handleMessageStart(
      msg => (
        _target = router.find(
          msg.head.headers.host,
          msg.head.path,
        )?.next?.()
      )
    )
    .branch(
      () => Boolean(_target), (
        $=>$.muxHTTP(() => _target).to(
          $=>$.connect(() => _target.id)
        )
      ), (
        $=>$.replaceMessage(
          new Message({ status: 404 }, 'No route')
        )
      )
    )
  )

)()
