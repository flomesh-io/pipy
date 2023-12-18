((
  nl = pipy.solve('nl.js'),
  bpf = pipy.solve('bpf.js'),

  epFromIPPort = (ip, port) => `${ip}/${port}`,
  epFromUpstream = (upstream) => epFromIPPort(upstream.ip, upstream.port),
  ipFromString = (str) => new Netmask(str).toBytes(),

  links = {},
  neighbours = {},
  routes = [],
  unknownIPs = {},

  lookupIP = (ip) => (
    neighbours[ip] || (
      ranges.find(
        ({ mask }) => mask.contains(ip)
      )
    )
  ),

  upstreams = (
    (
      mapByID = {},
      mapByEP = {},
      idNext = 1,
      idPool = new algo.ResourcePool(() => idNext++),

    ) => ({
      byID: (id) => mapByID[id],

      make: (ip, port) => (
        mapByEP[epFromIPPort(ip, port)] ??= (
          (
            id = idPool.allocate(),
            nextHop = lookupIP(ip),
          ) => (
            nextHop || (unknownIPs[ip] = true),
            bpf.maps.upstreams.update({ id }, {
              addr: {
                ip: ipFromString(ip),
                port,
              },
              interface: nextHop?.interface,
              mac: nextHop?.mac,
            }),
            mapByID[id] = { ip, port, id }
          )
        )()
      ),

      cleanup: (upstreams) => (
        Object.keys(mapByID).forEach(
          id => (id in mapByID) && (
            delete mapByID[id],
            delete mapByEP(epFromUpstream[upstreams[id]]),
            idPool.free(id),
            bpf.maps.upstreams.delete({ id })
          )
        )
      ),
    })
  )(),

  newLink = (link) => (
    console.log('new link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index)
  ),

  delLink = (link) => (
    console.log('del link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index)
  ),

  newAddress = (addr) => (
    console.log('new addr:', 'address', addr.address, 'index', addr.index)
  ),

  delAddress = (addr) => (
    console.log('del addr:', 'address', addr.address, 'index', addr.index)
  ),

  newRoute = (route) => (
    console.log('new route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif)
  ),

  delRoute = (route) => (
    console.log('del route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif)
  ),

  newNeighbour = (neigh) => (
    console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr)
  ),

  delNeighbour = (neigh) => (
    console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr)
  ),

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

  updateBalancers: (balancers) => (
    (
      newBalancers = {},
      newUpstreams = {},
    ) => (
      balancers.forEach(
        ({ ip, port, targets }) => (
          newBalancers[epFromIPPort(ip, port)] = {
            ip, port,
            targets: targets.map(
              ({ ip, port, weight }) => (
                (
                  upstream = upstreams.make(ip, port)
                ) => (
                  newUpstreams[upstream.id] = upstream,
                  { upstream, weight }
                )
              )()
            )
          }
        )
      ),
      upstreams.cleanup(newUpstreams)
    )
  )(),

  handleRouteChange: (msg) => (
    select(msg.head.type,
      16, () => newLink(nl.link.decode(msg.body)), // RTM_NEWLINK
      17, () => delLink(nl.link.decode(msg.body)), // RTM_DELLINK
      20, () => newAddress(nl.addr.decode(msg.body)), // RTM_NEWADDR
      21, () => delAddress(nl.addr.decode(msg.body)), // RTM_DELADDR
      24, () => newRoute(nl.route.decode(msg.body)), // RTM_NEWROUTE
      25, () => delRoute(nl.route.decode(msg.body)), // RTM_DELROUTE
      28, () => newNeighbour(nl.neigh.decode(msg.body)), // RTM_NEWNEIGH
      29, () => delNeighbour(nl.neigh.decode(msg.body)), // RTM_DELNEIGH
    )
  ),

})

)()
