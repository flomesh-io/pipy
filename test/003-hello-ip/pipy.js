pipy()

.listen(6080)
  .decodeHttpRequest()
  .replaceMessage(
    () => new Message(`Hello, ${__inbound.remoteAddress}!\n`)
  )
  .encodeHttpResponse()
