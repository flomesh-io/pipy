(config =>

pipy({
  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => [
          k, new algo.RoundRobinLoadBalancer(v)
        ]
      )
    )
  ),

  _target: '',
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'router',
})

.pipeline('request')
  .handleMessageStart(
    () => (
      _target = _services[__serviceID]?.select?.(),
      _target && (__turnDown = true)
    )
  )
  .link(
    'forward', () => Boolean(_target),
    ''
  )

.pipeline('forward')
  .muxHTTP(
    'connection',
    () => _target
  )

.pipeline('connection')
  .connect(
    () => _target
  )

)(JSON.decode(pipy.load('config/balancer.json')))
