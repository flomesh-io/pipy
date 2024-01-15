((
  nl = pipy.solve('nl.js'),

  obj = bpf.object(os.readFile('../../../bin/load-balancer.o')),
  program = obj.programs[0].load(6, 'GPL'),
  maps = Object.fromEntries(
    obj.maps.map(
      m => [m.name.startsWith('map_') ? m.name.substring(4) : m.name, m]
    )
  ),

  epFromIPPort = (ip, port) => `${ip}/${port}`,
  epFromUpstream = (upstream) => epFromIPPort(upstream.ip, upstream.port),
  ipFromString = (str) => ({ v4: { u8: new Netmask(str).toBytes() }}),
  macFromString = (str) => str.split(':').map(x => Number.parseInt(x, 16)),

  pendingRequests = [],

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
            maps.upstreams.update({ i: id }, {
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
            maps.upstreams.delete({ id })
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

  initLink = (index) => (
    links[index] || (
      pendingRequests.push(
        new Message(
          {
            type: 19, // RTM_SETLINK
            flags: 0x01 | 0x04, // NLM_F_REQUEST | NLM_F_ACK
          },
          nl.link.encode({
            index,
            attrs: {
              [43]: { // IFLA_XDP
                [1]: nl.i32(program.fd), // IFLA_XDP_FD
                [3]: nl.i32(1 << 1), // IFLA_XDP_FLAGS: XDP_FLAGS_SKB_MODE
              },
            }
          })
        )
      ),
      links[index] = {}
    )
  ),

  newLink = (link) => (
    console.log('new link:', 'address', link.address, 'broadcast', link.broadcast, 'ifname', link.ifname, 'index', link.index),
    maps.links.update(
      { i: link.index },
      Object.assign(initLink(link.index), { mac: macFromString(link.address) }),
    )
  ),

  delLink = (link) => (
    console.log('del link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index),
    maps.links.delete({ i: link.index })
  ),

  newAddress = (addr) => (
    console.log('add addr:', 'address', addr.address, 'prefixlen', addr.prefixlen, 'index', addr.index),
    new Netmask(addr.address).version === 4 && (
      maps.links.update(
        { i: addr.index },
        Object.assign(initLink(addr.index), { ip: ipFromString(addr.address) }),
      )
    )
  ),

  delAddress = (addr) => (
    console.log('del addr:', 'address', addr.address, 'prefixlen', addr.prefixlen, 'index', addr.index)
  ),

  newRoute = (route) => (
    console.log('new route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif, 'gateway', route.gateway),
    route.gateway && maps.routes.update(routeKey(route), ipFromString(route.gateway))
  ),

  delRoute = (route) => (
    console.log('del route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif),
    maps.routes.delete(routeKey(route))
  ),

  newNeighbour = (neigh) => (
    console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr, 'ifindex', neigh.ifindex),
    maps.neighbours.update(
      ipFromString(neigh.dst), {
        interface: neigh.ifindex,
        mac: macFromString(neigh.lladdr),
      }
    )
  ),

  delNeighbour = (neigh) => (
    console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr, 'ifindex', neigh.ifindex),
    maps.neighbours.delete(ipFromString(neigh.dst))
  ),

) => ({

  initialRequests: () => [
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

  pendingRequests: () => pendingRequests.splice(0),

  cleanupRequests: () => Object.keys(links).map(
    index => new Message(
      {
        type: 19, // RTM_SETLINK
        flags: 0x01 | 0x04, // NLM_F_REQUEST | NLM_F_ACK
      },
      nl.link.encode({
        index,
        attrs: {
          [43]: { // IFLA_XDP
            [1]: nl.i32(-1), // IFLA_XDP_FD
          },
        }
      })
    )
  ),

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
                maps.balancers.update(
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
              maps.balancers.delete(balancerKey(v.ip, v.port, 6)),
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
