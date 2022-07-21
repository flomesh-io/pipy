((
  config = JSON.decode(pipy.load('config.json')),
  router = new algo.URLRouter(config.routes),
  services = Object.fromEntries(
    Object.entries(config.services).map(
      ([k, v]) => [
        k, new algo.RoundRobinLoadBalancer(v)
      ]
    )
  ),

) => pipy({
  _target: undefined,
})

  .listen(config.listen)
  .demuxHTTP().to(
    $=>$
    .handleMessageStart(
      msg => (
        ((
          s = router.find(
            msg.head.headers.host,
            msg.head.path,
          )
        ) => (
          _target = services[s]?.next?.()
        ))()
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
