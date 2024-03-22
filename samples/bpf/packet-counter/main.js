var obj = bpf.object(pipy.load('packet-counter.o'))
var prog = obj.programs[0].load('BPF_PROG_TYPE_XDP')
var map = obj.maps[0]

var PIN_PATH = '/sys/fs/bpf/packet-counter'

pipy.listen(8080, $=>$
  .serveHTTP(
    new Message('hello!\n')
  )
)

// We don't have to rely on iproute2 to do the attaching.
// We may as well do it directly by talking to Netlink directly.
// However, we'll just go with iproute2 here for simplicity
// as our focus now is on eBPF, not Netlink.
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
    os.rm(PIN_PATH, { force: true })
  }
)
