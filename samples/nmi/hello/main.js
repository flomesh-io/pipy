pipy()
  .listen(8080)
  .demuxHTTP().to(
    $=>$.use('../../../bin/hello-nmi.so')
  )
