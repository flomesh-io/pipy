(config =>

pipy({
  _services: config.services,
  _rateLimit: undefined,
})

.import({
  __serviceID: 'router',
})

.pipeline('request')
  .handleStreamStart(
    () => _rateLimit = _services[__serviceID]?.rateLimit
  )
  .link(
    'throttle', () => Boolean(_rateLimit),
    'bypass'
  )

.pipeline('throttle')
  .throttleMessageRate(
    () => _rateLimit,
    () => __serviceID,
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('config/throttle.json')))
