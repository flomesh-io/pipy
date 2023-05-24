pipy()

.listen(8080)
.serveHTTP(
  new Message('hello')
)
