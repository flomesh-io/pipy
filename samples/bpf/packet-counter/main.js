var obj = bpf.object(
  os.readFile('../../../bin/packet-counter.o')
)

var prog = obj.programs[0].load(6, 'GPL') // BPF_PROG_TYPE_XDP
var map = obj.maps[0]

var PINNING_PATHNAME = '/sys/fs/bpf/packet-counter'

pipy.listen(8080, $=>$
  .serveHTTP(
    new Message('hello!\n')
  )
)

bpf.pin(PINNING_PATHNAME, prog.fd),
pipy.exec(`ip link set dev lo xdpgeneric pinned ${PINNING_PATHNAME}`)
dumpStats()

function dumpStats() {
  println('Packet stats:')
  map.entries().forEach(
    ([k, v]) => (
      println(' ', k.ip.u8.join('.'), v.i)
    )
  )
  new Timeout(1).wait().then(dumpStats)
}

pipy.exit(
  function () {
    pipy.exec('ip link set dev lo xdpgeneric off')
    pipy.exec(`rm ${PINNING_PATHNAME}`)
  }
)
