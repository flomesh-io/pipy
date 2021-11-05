pipy({
  _BATCH_SIZE: 1000,
  _BATCH_TIMEOUT: 5000,
  _CONTENT_TYPES: {
    '': true,
    'text/plain': true,
    'text/html': true,
    'application/json': true,
    'application/xml': true,
    'multipart/form-data': true,
  },

  _g: {
    buffer: new Data,
    bufferSize: 0,
    bufferTime: 0,
  },

  _queue: null,
  _contentType: '',
  _startTime: 0,
  _responseTime: 0,
})

// HTTP inbound
.listen(6080)
  .handleStreamStart(
    () => _queue = []
  )
  .fork('in')
  .connect('127.0.0.1:8080')
  .fork('out')

// Extract request info
.pipeline('in')
  .decodeHTTPRequest()
  .handleMessageStart(
    e => (
      _startTime = Date.now(),
      _contentType = e.head.headers['content-type'] || '',
      _contentType = _contentType.split(';')[0]
    )
  )
  .handleMessage(
    msg => _queue.push({
      startTime: _startTime,
      ...msg.head,
      body: _CONTENT_TYPES[_contentType] ? msg.body.toString() : null,
    })
  )

// Extract response info
.pipeline('out')
  .decodeHTTPResponse()
  .handleMessageStart(
    e => (
      _responseTime = Date.now(),
      _contentType = e.head.headers['content-type'] || '',
      _contentType = _contentType.split(';')[0]
    )
  )
  .replaceMessage(
    msg => new Message(
      JSON.encode({
        ..._queue.shift(),
        latency: _responseTime - _startTime,
        endTime: Date.now(),
        response: {
            ...msg.head,
            body: _CONTENT_TYPES[_contentType] ? msg.body.toString() : null,
        },
      }).push('\n')
    )
  )
  .link('batch')

// Accumulate log messages into batches
.pipeline('batch')
  .replaceMessage(
    msg => (
      msg.body.size > 0 && (
        _g.buffer.push('{"index":{}}\n'),
        _g.buffer.push(msg.body),
        _g.bufferSize++
      ),
      (_g.bufferSize >= _BATCH_SIZE ||
      (_g.bufferSize > 0 && Date.now() - _g.bufferTime > _BATCH_TIMEOUT)) ? (
        new Message(_g.buffer)
      ) : (
        null
      )
    )
  )
  .handleMessageStart(
    () => (
      _g.buffer = new Data,
      _g.bufferSize = 0,
      _g.bufferTime = Date.now()
    )
  )
  .mux('mux')

// Shared logging session
.pipeline('mux')
  .encodeHTTPRequest({
    method: 'POST',
    path: '/cars/_doc/_bulk',
    headers: {"Authorization": 'Basic ZWxhc3RpYzoxMjM0NTY=', "Content-Type": "application/json" }
  }) 
  .connect('47.110.85.213:9200')
  .decodeHTTPResponse() 

// Regularly flush the logging session
.task('1s')
  .fork('batch')
  .replaceMessage(
    () => new StreamEnd
  )

// Mock logging service on port 8123
.listen(8123)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('OK')
  )
  .encodeHTTPResponse()

// Mock service on port 8080
.listen(8080)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hi, there!\n' + Date.now())
  )
  .encodeHTTPResponse()