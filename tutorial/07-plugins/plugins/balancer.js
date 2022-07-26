((
  config = pipy.solve('config.js'),
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

  .import({
    __route: 'main',
  })

  .pipeline()
  .branch(
    () => Boolean(_target = services[__route]?.next?.()), (
      $=>$
      .muxHTTP(() => _target).to(
        $=>$.connect(() => _target.id)
      )
    ),
    $=>$.chain()
  )

)()
