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
    __serviceID: 'router',
  })

  .pipeline()
  .branch(
    () => Boolean(_target = services[__serviceID]?.next?.()), (
      $=>$
      .muxHTTP(() => _target).to(
        $=>$.connect(() => _target.id)
      )
    ),
    $=>$.chain()
  )

)()
