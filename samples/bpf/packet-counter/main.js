((
  obj = bpf.object(
    os.readFile('../../../bin/packet-counter.o')
  ),

  prog = obj.programs[0].load(6, 'GPL'),

  map = obj.maps[0],

  PINNING_PATHNAME = '/sys/fs/bpf/packet-counter',

  main = (ctx) => (
    bpf.pin(PINNING_PATHNAME, prog.fd),
    pipy.exec(`ip link set dev lo xdpgeneric pinned ${PINNING_PATHNAME}`),
    pipy(ctx)
  ),

) => main()

.listen(8080)
  .serveHTTP(new Message('hello'))

.task('1s')
  .onStart(
    () => (
      console.log('Packet stats:'),
      map.entries().forEach(
        ([k, v]) => (
          console.log(' ', k.ip.u8.join('.'), v.i)
        )
      ),
      new StreamEnd
    )
  )

.exit(() => (
  pipy.exec('ip link set dev lo xdpgeneric off'),
  pipy.exec(`rm ${PINNING_PATHNAME}`),
  new StreamEnd
))

)()
