(config =>

pipy()

.export('proxy', {
  __turnDown: false,
})

.listen(config.listen)
  .demuxHTTP('request')

.pipeline('request')
  .use(
    config.plugins,
    'request',
    'response',
    () => !__turnDown
  )

)(JSON.decode(pipy.load('config/proxy.json')))
