pipy()

.listen(8000)
  .demuxHTTP('req')

.pipeline('req')
  .muxHTTP(
    'conn',
    {
      version: 2,
    }
  )

.pipeline('conn')
  .connect('localhost:8080')
