((
  map = bpf.Map.open(
    bpf.Map.list().find(
      i => i.name === 'pkt_cnt_stats'
    )?.id,
    new CStruct().field('uint8[4]', 'ip'),
    new CStruct().field('uint32', 'count'),
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
