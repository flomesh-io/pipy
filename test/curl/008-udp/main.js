pipy()

.listen(9090, { protocol: 'udp', idleTimeout: 3 })
.replaceData(
  new Data('Hello!\n')
)

.listen(8080)
.demuxHTTP().to($=>$
  .replaceMessage(
    new Data('hi')
  )
  .connect('localhost:9090', { protocol: 'udp' })
  .replaceData(
    data => new Message(data)
  )
)
