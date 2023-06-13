((
  map = bpf.Map.open(
    bpf.Map.list().find(
      i => i.name === 'pkt_cnt_stats'
    )?.id,
    new CStruct().field('uint32', 'ip'),
    new CStruct().field('uint32', 'count'),
  ),

) => pipy()

.listen(8080)
.serveHTTP(new Message('hello'))

.task()
.onStart(
  () => (
    map.update({ ip: 0x0100007f }, { count: 0 }),
    new StreamEnd
  )
)

.task('5s')
.onStart(
  () => (
    console.log(map.lookup({ ip: 0x0100007f })),
    new StreamEnd
  )
)

)()
