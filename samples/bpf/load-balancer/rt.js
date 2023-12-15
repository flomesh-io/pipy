((
  nl = pipy.solve('nl.js'),
) => ({

  initialRequests: [
    new Message(
      {
        type: 18, // RTM_GETLINK
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.link.encode({
        family: 0, // FAMILY_ALL
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
      nl.addr.encode({
        family: 0, // FAMILY_ALL
        index: 0,
      })
    ),
    new Message(
      {
        type: 26, // RTM_GETROUTE
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.addr.encode({
        family: 0, // FAMILY_ALL
        index: 0,
      })
    ),
    new Message(
      {
        type: 30, // RTM_GETNEIGH
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.addr.encode({
        family: 0, // FAMILY_ALL
        index: 0,
      })
    ),
  ],

  handleRouteChange: (msg) => (
    select(msg.head.type,
      16, () => ( // RTM_NEWLINK
        console.log('new link:', nl.link.decode(msg.body))
      ),
      17, () => ( // RTM_DELLINK
        console.log('del link:', nl.link.decode(msg.body))
      ),
      20, () => ( // RTM_NEWADDR
        console.log('new addr:', nl.addr.decode(msg.body))
      ),
      21, () => ( // RTM_DELADDR
        console.log('del addr:', nl.addr.decode(msg.body))
      ),
      24, () => ( // RTM_NEWROUTE
        console.log('new route:', nl.route.decode(msg.body))
      ),
      28, () => ( // RTM_NEWNEIGH
        console.log('new neigh:', nl.neigh.decode(msg.body))
      ),
    )
  ),

})

)()
