((
  router = new algo.URLRouter({
    '/hi/*': 'localhost:8080',
    '/echo': 'localhost:8081',
    '/ip/*': 'localhost:8082',
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
        )
      )
    )
    .branch(
      () => Boolean(_target), (
        $=>$.muxHTTP(() => _target).to(
          $=>$.connect(() => _target)
        )
      ), (
        $=>$.replaceMessage(
          new Message({ status: 404 }, 'No route')
        )
      )
    )
  )

)()
