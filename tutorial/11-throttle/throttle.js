(config =>

pipy({
  _services: config.services,
  _rateLimit: undefined,
})

.import({
  __serviceID: 'router',
})

.pipeline('request')
  .handleSessionStart(
    () => _rateLimit = _services[__serviceID]?.rateLimit
  )
  .link(
    'throttle', () => Boolean(_rateLimit),
    'bypass'
  )

.pipeline('throttle')
  .tap(
    () => _rateLimit,
    () => __serviceID,
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('throttle.json')))