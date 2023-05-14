pipy()

.pipeline('hello')
.serveHTTP(
  new Message('Hello!\n')
)

.listen(8080)
.link('hello')

.listen(8081, { maxConnections: 1 })
.link('hello')

.listen(8082)
.onStart(new StreamEnd)

.listen(8000)
.connect('localhost:8080', { retryCount: 3, retryDelay: 1 })

.listen(8001)
.connect('localhost:8081', { retryCount: 3, retryDelay: 1 })

.listen(8002)
.connect('localhost:8082', { retryCount: 3, retryDelay: 1 })

.listen(8003)
.connect('localhost:8083', { retryCount: 3, retryDelay: 1 })

.task()
.onStart(new Data)
.fork().to(
  $=>$.connect('localhost:8081')
)
.replaceStreamStart(
  () => new Timeout(5).wait().then(new StreamEnd)
)
