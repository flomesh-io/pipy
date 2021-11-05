pipy({
  _metrics: {
    count: 0,
  },
  _statuses: {},
  _latencies: [
    1,2,5,7,10,15,20,25,30,40,50,60,70,80,90,100,
    200,300,400,500,1000,2000,5000,10000,30000,60000,
    Number.POSITIVE_INFINITY
  ],
  _buckets: [],
  _timestamp: 0,
})

.listen(6080)
  .fork('in')
  .connect('127.0.0.1:8080')
  .fork('out')

// Extract request info
.pipeline('in')
  .decodeHTTPRequest()
  .handleMessageStart(
    () => (
      _timestamp = Date.now(),
      _metrics.count++
    )
  )

// Extract response info
.pipeline('out')
  .decodeHTTPResponse()
  .handleMessageStart(
    e => (
      ((status, latency, i) => (
        status = e.head.status,
        latency = Date.now() - _timestamp,
        i = _latencies.findIndex(t => latency <= t),
        _buckets[i]++,
        _statuses[status] = (_statuses[status]|0) + 1
      ))()
    )
  )

// Expose as Prometheus metrics
.listen(9090)
  .decodeHTTPRequest()
  .replaceMessage(
    () => (
      (sum => new Message(
        [
          `count ${_metrics.count}`,
          ...Object.entries(_statuses).map(
            ([k, v]) => `status{code="${k}"} ${v}`
          ),
          ..._buckets.map((n, i) => `bucket{le="${_latencies[i]}"} ${sum += n}`)
        ]
        .join('\n')
      ))(0)
    )
  )
  .encodeHTTPResponse()

// Mock service on port 8080
.listen(8080)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello!\n')
  )
  .encodeHTTPResponse()