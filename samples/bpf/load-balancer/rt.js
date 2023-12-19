((
  nl = pipy.solve('nl.js'),
  bpf = pipy.solve('bpf.js'),

  epFromIPPort = (ip, port) => `${ip}/${port}`,
  epFromUpstream = (upstream) => epFromIPPort(upstream.ip, upstream.port),
  ipFromString = (str) => ({ v4: new Netmask(str).toBytes() }),

  links = {},

  upstreams = (
    (
      mapByID = {},
      mapByEP = {},
      idNext = 1,
      idPool = new algo.ResourcePool(() => idNext++),

    ) => ({
      make: (ip, port) => (
        mapByEP[epFromIPPort(ip, port)] ??= (
          (
            id = idPool.allocate()
          ) => (
            bpf.maps.upstreams.update({ id }, {
              addr: {
                ip: ipFromString(ip),
                port,
              },
            }),
            mapByID[id] = { ip, port, id }
          )
        )()
      ),

      cleanup: (upstreams) => (
        Object.keys(mapByID).forEach(
          id => (id in upstreams) || (
            delete mapByID[id],
            delete mapByEP[epFromUpstream[upstreams[id]]],
            idPool.free(id),
            bpf.maps.upstreams.delete({ id })
          )
        )
      ),
    })
  )(),

  routeKey = (route) => ({
    mask: { prefixlen: route.dst_len },
    ip: ipFromString(route.dst || '0.0.0.0'),
  }),

  balancerKey = (ip, port, proto) => ({
    addr: {
      ip: ipFromString(ip),
      port,
    },
    proto,
  }),

  newLink = (link) => (
    console.log('new link:', 'address', link.address, 'broadcast', link.broadcast, 'ifname', link.ifname, 'index', link.index),
    links[link.index] = link
  ),

  delLink = (link) => (
    console.log('del link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index),
    delete links[link.index]
  ),

  newRoute = (route) => (
    (
      link = links[route.oif]
    ) => (
      console.log('new route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif),
      bpf.maps.routes.update(routeKey(route), {
        interface: link.index,
        broadcast: link.broadcast.split(':').map(x => Number.parseInt(x, 16)),
      })
    )
  )(),

  delRoute = (route) => (
    console.log('del route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif),
    bpf.maps.routes.delete(routeKey(route))
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

  setupBalancers: (
    (
      oldBalancers = {}
    ) => (
      (balancers) => (
        (
          newBalancers = {},
          newUpstreams = {},
        ) => (
          console.log('Setting up balancers...'),
          balancers.forEach(
            ({ ip, port, targets }) => (
              (
                balancer = {
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
              ) => (
                console.log('  Update balancer', ip, port),
                newBalancers[epFromIPPort(ip, port)] = balancer,
                bpf.maps.balancers.update(
                  balancerKey(ip, port, 6), {
                    ring: [balancer.targets[0].upstream.id],
                    hint: 0,
                  }
                )
              )
            )()
          ),
          Object.entries(oldBalancers).forEach(
            ([k, v]) => k in newBalancers || (
              bpf.maps.balancers.delete(balancerKey(v.ip, v.port, 6)),
              console.log(' Delete balancer', ip, port)
            )
          ),
          upstreams.cleanup(newUpstreams),
          oldBalancers = newBalancers,
          console.log('Balancers updated')
        )
      )()
    )
  )(),

  handleRouteChange: (msg) => (
    select(msg.head.type,
      16, () => newLink(nl.link.decode(msg.body)), // RTM_NEWLINK
      17, () => delLink(nl.link.decode(msg.body)), // RTM_DELLINK
      24, () => newRoute(nl.route.decode(msg.body)), // RTM_NEWROUTE
      25, () => delRoute(nl.route.decode(msg.body)), // RTM_DELROUTE
      28, () => newNeighbour(nl.neigh.decode(msg.body)), // RTM_NEWNEIGH
      29, () => delNeighbour(nl.neigh.decode(msg.body)), // RTM_DELNEIGH
    )
  ),

})

)()
