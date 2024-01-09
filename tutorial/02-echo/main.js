pipy()

  .listen(8080)
  .serveHTTP(new Message('Hi, there!\n'))

  .listen(8081)
  .serveHTTP(msg => new Message(msg.body))

  .listen(8082)
  .serveHTTP(
    msg => new Message(
      `You are requesting ${msg.head.path} from ${__inbound.remoteAddress}\n`
    )
  )
