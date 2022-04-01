pipy()

.listen(8000)
  .demuxHTTP('req')

.pipeline('req')
  .muxHTTP('conn', () => __inbound)

.pipeline('conn')
  .connect('localhost:8080')
