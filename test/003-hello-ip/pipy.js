pipy()

.listen(6080)
  .decodeHTTPRequest()
  .replaceMessage(
    () => new Message(`Hello, ${__inbound.remoteAddress}!\n`)
  )
  .encodeHTTPResponse()
