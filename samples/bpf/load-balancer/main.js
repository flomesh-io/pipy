((
  rt = pipy.solve('rt.js'),

  setupBalancers = () => (
    rt.setupBalancers(JSON.decode(pipy.load('config.json')).balancers)
  ),

  main = (ctx) => (
    setupBalancers(),
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
    bind: `pid=0;groups=${1|4|0x40|0x400}`, // groups = RTMGRP_LINK | RTMGRP_NEIGH | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE
  })
  .decodeNetlink()
  .handleMessage(rt.handleRouteChange)

.watch('config.json')
  .onStart(() => (
    setupBalancers(),
    new StreamEnd
  ))

.listen(8080)
  .serveHTTP(new Message('hi'))

)()
