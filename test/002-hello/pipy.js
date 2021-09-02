pipy()

.listen(6080)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello!\n')
  )
  .encodeHTTPResponse()