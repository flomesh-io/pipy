pipy()

  .task()
  .onStart(new Message)
  .read('input')
  .decodeHTTPResponse()
  .decodeMultipart()
  .encodeHTTPResponse()
  .tee('-')
