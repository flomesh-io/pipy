pipy()

.listen(6080)
  .connect(
    '127.0.0.1:6443',
    {
      tls: {},
    }
  )

.listen(6443, {
  tls: {
    cert: os.readFile('cert.pem').toString(),
    key: os.readFile('key.pem').toString(),
  },
})
  .decodeHttpRequest()
  .replaceMessage(
    new Message('Hello!\n')
  )
  .encodeHttpResponse()