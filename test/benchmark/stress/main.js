((
  TARGET_URL = os.env.URL || 'http://localhost:8000',
  METHOD = os.env.METHOD || 'GET',
  PAYLOAD_SIZE = os.env.PAYLOAD_SIZE | 0,
  CONCURRENCY = (os.env.CONCURRENCY|0) || 1,

  url = new URL(TARGET_URL),
  counts = new stats.Counter('counts', ['status']),
  latency = new stats.Histogram('latency', [
    ...new Array(30).fill().map((_,i)=>i+1),
    Number.POSITIVE_INFINITY,
  ]),

) =>

pipy({
  _session: null,
  _time: 0,
})

.task()
.onStart(
  new Message(
    {
      method: METHOD,
      path: url.path,
    },
    new Data(new Array(PAYLOAD_SIZE).fill(0x30))
  )
)
.fork(new Array(CONCURRENCY).fill()).to(
  $=>$
  .onStart(() => void (_session = {}))
  .replay().to(
    $=>$
    .handleMessageStart(() => _time = Date.now())
    .muxHTTP(() => _session).to(
      $=>$.connect(url.host)
    )
    .handleMessageStart(() => latency.observe(Date.now() - _time))
    .replaceMessage(
      msg => (
        counts.increase(),
        counts.withLabels(msg.head.status).increase(),
        new StreamEnd('Replay')
      )
    )
  )
)

)()
