((
  nl = pipy.solve('nl.js'),
  bpf = pipy.solve('bpf.js'),

  epFromIPPort = (ip, port) => `${ip}/${port}`,
  epFromUpstream = (upstream) => epFromIPPort(upstream.ip, upstream.port),
  ipFromString = (str) => ({ v4: new Netmask(str).toBytes() }),
  macFromString = (str) => str.split(':').map(x => Number.parseInt(x, 16)),

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
    bpf.maps.links.update(
      { id: link.index },
      Object.assign(links[link.index] ??= {}, { mac: macFromString(link.address) }),
    )
  ),

  delLink = (link) => (
    console.log('del link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index),
    bpf.maps.links.delete({ id: link.index })
  ),

  newAddress = (addr) => (
    console.log('add addr:', 'address', addr.address, 'prefixlen', addr.prefixlen, 'index', addr.index),
    new Netmask(addr.address).version === 4 && (
      bpf.maps.links.update(
        { id: addr.index },
        Object.assign(links[addr.index] ??= {}, { ip: ipFromString(addr.address) }),
      )
    )
  ),

  delAddress = (addr) => (
    console.log('del addr:', 'address', addr.address, 'prefixlen', addr.prefixlen, 'index', addr.index)
  ),

  newRoute = (route) => (
    console.log('new route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif, 'gateway', route.gateway),
    route.gateway && bpf.maps.routes.update(routeKey(route), ipFromString(route.gateway))
  ),

  delRoute = (route) => (
    console.log('del route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif),
    bpf.maps.routes.delete(routeKey(route))
  ),

  newNeighbour = (neigh) => (
    console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr, 'ifindex', neigh.ifindex),
    bpf.maps.neighbours.update(
      ipFromString(neigh.dst), {
        interface: neigh.ifindex,
        mac: macFromString(neigh.lladdr),
      }
    )
  ),

  delNeighbour = (neigh) => (
    console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr, 'ifindex', neigh.ifindex),
    bpf.maps.neighbours.delete(ipFromString(neigh.dst))
  ),

) => ({

  initialRequests: [
    new Message(
      {
        type: 18, // RTM_GETLINK
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.link.encode({})
    ),
    new Message(
      {
        type: 22, // RTM_GETADDR
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.addr.encode({})
    ),
    new Message(
      {
        type: 26, // RTM_GETROUTE
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.route.encode({})
    ),
    new Message(
      {
        type: 30, // RTM_GETNEIGH
        flags: 0x01 | 0x0100 | 0x0200, // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH
      },
      nl.neigh.encode({})
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
              console.log('  Delete balancer', v.ip, v.port)
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
