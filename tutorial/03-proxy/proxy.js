pipy()

.listen(8000)
  .demuxHTTP('forward')

.pipeline('forward')
  .muxHTTP('connection')

.pipeline('connection')
  .connect('localhost:8080')
