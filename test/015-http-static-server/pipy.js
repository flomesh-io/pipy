pipy({
  _cache: new algo.Cache(
    path => http.File.from('www' + path)
  ),
  _file: undefined,
})

// Inbound file request
.listen(6080)
  .decodeHTTPRequest()
  .handleMessageStart(
    e => _file = _cache.get(e.head.path)
  )
  .wait(
    () => _file !== undefined
  )
  .link(
    'serve', () => _file,
    '404'
  )
  .encodeHTTPResponse()

// Serve the file
.pipeline('serve')
  .replaceMessage(
    msg => _file.toMessage(msg.head.headers['accept-encoding'])
  )

// Not found
.pipeline('404')
  .replaceMessage(
    new Message({ status: 404 })
  )