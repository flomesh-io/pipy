((
  rtnl = pipy.solve('rtnl.js'),

) => pipy()

.task()
.onStart(new Data)
.fork([
  new Message(
    {
      type: 18, // RTM_GETLINK
      flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
    },
    rtnl.link.encode({
      family: 0, // AF_UNSPEC
      attrs: {
        [29]: new Data([1, 0, 0, 0]), // IFLA_EXT_MASK: RTEXT_FILTER_VF
      }
    })
  ),
  new Message(
    {
      type: 22, // RTM_GETADDR
      flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
    },
    rtnl.addr.encode({
      family: 0, // FAMILY_ALL
      index: 0,
    })
  ),
]).to($=>$
  .onStart(msg => msg)
  .encodeNetlink()
  .connect('pid=0;groups=0', {
    protocol: 'netlink',
    netlinkFamily: 0,
  })
  .decodeNetlink()
  .link('on-netlink-update')
)

.task()
.onStart(new Data)
.connect('pid=0;groups=0', {
  protocol: 'netlink',
  netlinkFamily: 0,
  bind: `pid=0;groups=${1|0x10|0x100}`, // groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR
})
.decodeNetlink()
.link('on-netlink-update')

.pipeline('on-netlink-update')
.handleMessage(
  msg => select(msg.head.type,
    16, () => ( // RTM_NEWLINK
      console.log('new link:', rtnl.link.decode(msg.body))
    ),
    17, () => ( // RTM_DELLINK
      console.log('del link:', rtnl.link.decode(msg.body))
    ),
    20, () => ( // RTM_NEWADDR
      console.log('new addr:', rtnl.addr.decode(msg.body))
    ),
    21, () => ( // RTM_DELADDR
      console.log('del addr:', rtnl.addr.decode(msg.body))
    ),
  )
)

)()
