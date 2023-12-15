((
  bpf = pipy.solve('bpf.js'),
  rt = pipy.solve('rt.js'),

  main = (ctx) => (
    bpf.maps.neighbors.update(
      { id: 0 },
      { mac: [0, 0, 0, 0, 0, 0] },
    ),

    bpf.maps.upstreams.update(
      { id: 0 },
      { addr: { ip: { v4: [127, 0, 0, 1] }, port: 8080 }, neighbor: 0 },
    ),

    bpf.maps.balancers.update(
      { addr: { ip: { v4: [127, 0, 0, 1] }, port: 8000 }, proto: 6 },
      { ring: [0], hint: 0 },
    ),

    pipy(ctx)
  ),

) => main()

.task()
  .onStart(new Data)
  .fork(rt.initialRequests).to($=>$
    .onStart(msg => msg)
    .encodeNetlink()
    .connect('pid=0;groups=0', {
      protocol: 'netlink',
      netlinkFamily: 0,
    })
    .decodeNetlink()
    .handleMessage(rt.handleRouteChange)
  )

.task()
  .onStart(new Data)
  .connect('pid=0;groups=0', {
    protocol: 'netlink',
    netlinkFamily: 0,
    bind: `pid=0;groups=${1|0x10|0x100}`, // groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR
  })
  .decodeNetlink()
  .handleMessage(rt.handleRouteChange)

.listen(8080)
  .serveHTTP(new Message('hi'))

)()
