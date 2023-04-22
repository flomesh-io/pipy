((
  config = JSON.decode(pipy.load('config.json')),
  url = new URL(config.url),
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
      method: config.method,
      path: url.path,
      headers: config.headers,
    },
    new Data(new Array(config.payloadSize).fill(0x30))
  )
)
.fork(new Array(config.concurrency).fill()).to(
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
