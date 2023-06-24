((
  options = { batch: { size: 0 }},
  logSlow = new logging.JSONLogger('test').toHTTP('http://localhost:8123/log', options).log,
  logBad = new logging.JSONLogger('test').toHTTP('http://localhost:8124/log', options).log,
  string1KB = ('x').repeat(1024),

) => pipy()

.task()
.onStart(new Data)
.replay().to($=>$
  .handleData(
    () => (
      logSlow({ data: string1KB }),
      logBad({ data: string1KB })
    )
  )
  .replaceData(
    new StreamEnd('Replay')
  )
)

.listen(8123)
.throttleDataRate(new algo.Quota(8192, { per: 1 }))
.serveHTTP(new Message('ok'))

)()
