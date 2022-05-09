pipy()

.listen(8000)
  .demuxHTTP('req')

.pipeline('req')
  .muxHTTP(
    'conn',
    () => __inbound,
    {
      version: 2,
    }
  )

.pipeline('conn')
  .dump('>>>')
  .connect('localhost:8080')
  .dump('<<<')
