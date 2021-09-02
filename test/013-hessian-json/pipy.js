pipy()

.listen(6080)
  .decodeHTTPRequest()
  .replaceMessageBody(
    body => (
      Hessian.encode(JSON.decode(body))
    )
  )
  .encodeDubbo()
  .connect('127.0.0.1:20880')
  .decodeDubbo()
  .replaceMessageBody(
    body => (
      JSON.encode(Hessian.decode(body), null, 2)
    )
  )
  .encodeHTTPResponse()

// Mock TCP echo server
.listen(20880)