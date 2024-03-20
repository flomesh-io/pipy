#!/usr/bin/env pipy

var obj = bpf.object(pipy.load('transparent-proxy.o'))
var progCgConnect4 = obj.programs.find(p => p.name === 'cg_connect4').load('BPF_PROG_TYPE_CGROUP_SOCK_ADDR', 'BPF_CGROUP_INET4_CONNECT')
var progCgSockOps = obj.programs.find(p => p.name === 'cg_sock_ops').load('BPF_PROG_TYPE_SOCK_OPS')
var progCgSockOpt = obj.programs.find(p => p.name === 'cg_sock_opt').load('BPF_PROG_TYPE_CGROUP_SOCKOPT', 'BPF_CGROUP_GETSOCKOPT')

var PROXY_PORT = 18000
var CGRP = '/sys/fs/cgroup'
var CGRP_PIPY = `${CGRP}/pipy`
var SOL_IP = 0
var SO_ORIGINAL_DST = 80

var sockaddr_in = new CStruct({
  sin_family: 'uint16',
  sin_port: 'uint8[2]',
  sin_addr: 'uint8[4]',
})

pipy.exec(`mkdir ${CGRP_PIPY}`)
os.writeFile(`${CGRP_PIPY}/cgroup.procs`, pipy.pid.toString())

obj.maps.find(m => m.name === 'map_config').update(
  { i: 0 }, {
    proxy_port: PROXY_PORT,
    pipy_cgroup_id: bpf.cgroup(CGRP_PIPY)
  }
)

bpf.attach('BPF_CGROUP_INET4_CONNECT', progCgConnect4.fd, CGRP)
bpf.attach('BPF_CGROUP_SOCK_OPS', progCgSockOps.fd, CGRP)
bpf.attach('BPF_CGROUP_GETSOCKOPT', progCgSockOpt.fd, CGRP)

pipy.exit(
  function() {
    bpf.detach('BPF_CGROUP_INET4_CONNECT', progCgConnect4.fd, CGRP)
    bpf.detach('BPF_CGROUP_SOCK_OPS', progCgSockOps.fd, CGRP)
    bpf.detach('BPF_CGROUP_GETSOCKOPT', progCgSockOpt.fd, CGRP)
    pipy.exec(`rm ${CGRP_PIPY}`)
  }
)

var $targetAddr
var $targetPort

pipy.listen(PROXY_PORT, $=>$
  .onStart(
    function (ib) {
      var od = new Data
      ib.socket.getRawOption(SOL_IP, SO_ORIGINAL_DST, od)
      var sa = sockaddr_in.decode(od)
      var addr = sa.sin_addr
      var port = sa.sin_port
      $targetAddr = addr.join('.')
      $targetPort = (port[0] << 8) | port[1]
    }
  )
  .fork().to($=>$
    .decodeHTTPRequest()
    .handleMessageStart(
      ({ head }) => println(head.method, head.path, head.headers.host)
    )
  )
  .connect(() => `${$targetAddr}:${$targetPort}`)
  .fork().to($=>$
    .decodeHTTPResponse()
    .handleMessageStart(
      ({ head }) => println(' ', head.status, head.statusText)
    )
  )
)
