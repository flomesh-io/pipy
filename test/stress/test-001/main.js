pipy()

  .listen(8000)
  .serveHTTP(
    new Message('hi')
  )
