pipy()

.listen(8080)
.demuxHTTP().to($=>$
  .replaceMessage(
    new Message(pipy.load('index.html'))
  )
  .compressHTTP('gzip')
)

.listen(8081)
.demuxHTTP().to($=>$
  .replaceMessage(
    new Message(pipy.load('index.html'))
  )
  .compressHTTP('deflate')
)

.listen(8000)
.demuxHTTP().to($=>$
  .muxHTTP().to($=>$
    .connect('localhost:8080')
  )
  .decompressHTTP()
)

.listen(8001)
.demuxHTTP().to($=>$
  .muxHTTP().to($=>$
    .connect('localhost:8081')
  )
  .decompressHTTP()
)
