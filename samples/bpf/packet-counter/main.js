var obj = bpf.object(
  os.readFile('../../../bin/packet-counter.o')
)

var prog = obj.programs[0].load('BPF_PROG_TYPE_XDP', 'GPL')
var map = obj.maps[0]

var PIN_PATH = '/sys/fs/bpf/packet-counter'

pipy.listen(8080, $=>$
  .serveHTTP(
    new Message('hello!\n')
  )
)

bpf.pin(PIN_PATH, prog.fd)
pipy.exec(`ip link set dev lo xdpgeneric pinned ${PIN_PATH}`)
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
    pipy.exec(`rm ${PIN_PATH}`)
  }
)
