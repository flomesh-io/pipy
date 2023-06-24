((
  data1KB = new Data(new Array(1024).fill(0xcc)),
  message1KB = new Message(data1KB),
  quota = new algo.Quota(1024*1024, { per: 1 }),

) => pipy()

.listen(8000)
.throttleDataRate(quota)
.dummy()

.listen(8001)
.onStart(new Data)
.replay().to($=>$
  .replaceStreamStart(
    [data1KB, new StreamEnd('Replay')]
  )
)

.task()
.onStart(message1KB)
.replay().to($=>$
  .fork().to($=>$
    .mux(() => 1, { outputCount: 0 }).to($=>$
      .connect('localhost:8000')
    )
  )
  .replaceMessage(
    new StreamEnd('Replay')
  )
)

.task()
.onStart(new Data)
.connect('localhost:8001')
.throttleDataRate(quota)
.dummy()

)()
