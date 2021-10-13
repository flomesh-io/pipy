(config =>

pipy()

.export('proxy', {
  __turnDown: false,
})

.listen(config.listen)
  .use(config.plugins, 'connect')
  .demuxHTTP('request')

.pipeline('request')
  .use(config.plugins, 'request', () => !__turnDown)
  .link(
    'bypass', () => __turnDown,
    'no-handler'
  )
  .use(config.plugins.slice().reverse(), 'response')

.pipeline('no-handler')
  .replaceMessage(
    new Message({ status: 404 }, 'No handler')
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('proxy.json')))
