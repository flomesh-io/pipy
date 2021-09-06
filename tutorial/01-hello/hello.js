pipy()

.listen(8080)
  .serveHTTP(
    msg => new Message(msg.body)
  )

.listen(8081)
  .demuxHTTP('tell-my-port')

.listen(8082)
  .demuxHTTP('tell-my-port')

.pipeline('tell-my-port')
  .replaceMessage(
    () => (
      new Message(`Hi, I'm on port ${__inbound.localPort}!\n`)
    )
  )