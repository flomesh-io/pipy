pipy()

.listen(8080)
  .decodeHttpRequest()
  .replaceMessage(
    new Message('Hi, there!\n')
  )
  .encodeHttpResponse()
