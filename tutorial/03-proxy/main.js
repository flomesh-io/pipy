pipy()

  .listen(8000)
  .demuxHTTP().to(
    $=>$.muxHTTP().to(
      $=>$.connect('localhost:8080')
    )
  )
