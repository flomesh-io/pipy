pipy()

.listen(6080)
  .decodeHttpRequest()
  .replaceMessage(
    new Message('Hello!\n')
  )
  .encodeHttpResponse()