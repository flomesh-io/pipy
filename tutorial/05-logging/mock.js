pipy()

.listen(8123)
  .serveHTTP(
    msg => console.log(msg.body.toString())
  )
  .dummy()