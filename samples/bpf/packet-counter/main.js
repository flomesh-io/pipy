((
  map = bpf.Map.open(
    bpf.Map.list().find(
      i => i.name === 'pkt_cnt_stats'
    )?.id,
    new CStruct({ ip: 'uint8[4]' }),
    new CStruct({ count: 'uint32' }),
  ),

) => pipy()

.listen(8080)
.serveHTTP(new Message('hello'))

.task('5s')
.onStart(
  () => (
    console.log('Packet stats:'),
    map.entries().forEach(
      ([k, v]) => (
        console.log(' ', k.ip.join('.'), v.count)
      )
    ),
    new StreamEnd
  )
)

)()
