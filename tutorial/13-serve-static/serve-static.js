(config =>

pipy({
  _root: '',
  _file: null,
})

.import({
  __turnDown: 'proxy',
  __serviceID: 'router',
})

.pipeline('request')
  .handleMessageStart(
    msg => (
      _root = config.services[__serviceID]?.root,
      _root && (
        _file = http.File.from(_root + msg.head.path)
      )
    )
  )
  .link(
    'serve', () => Boolean(_file),
    'bypass'
  )

.pipeline('serve')
  .replaceMessage(
    msg => (
      __turnDown = true,
      _file.toMessage(msg.head.headers['accept-encoding'])
    )
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('serve-static.json')))
