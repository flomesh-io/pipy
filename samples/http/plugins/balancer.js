((
  config = pipy.solve('config.js'),
  policies = {
    'round-robin': algo.RoundRobinLoadBalancer,
    'least-work': algo.LeastWorkLoadBalancer,
    'hashing': algo.HashingLoadBalancer,
  },
  balancers = (
    Object.fromEntries(
      Object.entries(config.upstreams).map(
        ([k, v]) => [
          k, new policies[v.policy](v.targets)
        ]
      )
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
    () => Boolean(_target = balancers[__route]?.next?.()), (
      $=>$.muxHTTP(() => _target).to(
        $=>$.connect(() => _target.id)
      )
    ), (
      $=>$.chain()
    )
  )

)()
