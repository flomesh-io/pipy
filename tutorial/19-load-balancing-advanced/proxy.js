(config =>

pipy()

.export('proxy', {
  __turnDown: false,
  __isTLS: false,
})

.listen(config.listen)
  .use(config.plugins, 'session')
  .demuxHTTP('request')

.pipeline('request')
  .use(
    config.plugins,
    'request',
    'response',
    () => __turnDown
  )

)(JSON.decode(pipy.load('config/proxy.json')))
