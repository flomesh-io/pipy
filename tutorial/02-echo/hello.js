pipy()

.listen(8080)
  .serveHTTP(
    new Message('Hi, there!\n')
  )

.listen(8081)
  .serveHTTP(
    msg => new Message(msg.body)
  )
