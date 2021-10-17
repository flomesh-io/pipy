(config =>

pipy({
  _services: (
    Object.fromEntries(
      Object.entries(config.services).map(
        ([k, v]) => [
          k,
          {
            ...v,
            balancer: new algo.RoundRobinLoadBalancer(v.targets),
          }
        ]
      )
    )
  ),

  _balancer: null,
  _balancerCache: null,
  _target: '',
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'router',
})

.pipeline('session')
  .handleSessionStart(
    () => (
      _balancerCache = new algo.Cache(
        // k is a balancer, v is a target
        (k  ) => k.select(),
        (k,v) => k.deselect(v),
      )
    )
  )
  .handleSessionEnd(
    () => (
      _balancerCache.clear()
    )
  )

.pipeline('request')
  .handleMessageStart(
    () => (
      _balancer = _services[__serviceID]?.balancer,
      _balancer && (__turnDown = true)
    )
  )
  .link(
    'load-balance', () => Boolean(_balancer),
    'bypass'
  )

.pipeline('load-balance')
  .handleMessageStart(
    () => (
      _target = _balancerCache.get(_balancer)
    )
  )
  .link(
    'forward', () => Boolean(_target),
    'no-target'
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

.pipeline('no-target')
  .replaceMessage(
    new Message({ status: 404 }, 'No target')
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('config/balancer.json')))
