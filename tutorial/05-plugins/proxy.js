pipy()

.export('proxy', {
  __turnDown: false,
})

.listen(8000)
  .demuxHTTP('request')

.pipeline('request')
  .use(
    [
      'plugins/router.js',
      'plugins/default.js',
    ],
    'request',
    'response',
    () => !__turnDown
  )
