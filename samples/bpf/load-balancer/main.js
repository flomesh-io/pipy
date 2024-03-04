import rtnl, { setLinkXDP } from './rtnl.js'

var obj = bpf.object(os.readFile('../../../bin/load-balancer.o'))
var program = obj.programs[0].load(6, 'GPL') // BPF_PROG_TYPE_XDP

var maps = Object.fromEntries(
  obj.maps.map(
    m => [m.name.startsWith('map_') ? m.name.substring(4) : m.name, m]
  )
)

var RING_SIZE = maps.balancers.valueType.reflect().ring.count

var epFromIPPort = (ip, port) => `${ip}/${port}`
var epFromUpstream = (upstream) => epFromIPPort(upstream.ip, upstream.port)
var ipFromString = (str) => ({ v4: { u8: new Netmask(str).toBytes() }})
var macFromString = (str) => str.split(':').map(x => Number.parseInt(x, 16))

var routeKey = (route) => ({
  mask: { prefixlen: route.dst_len },
  ip: ipFromString(route.dst || '0.0.0.0'),
})

var balancerKey = (ip, port, proto) => ({
  addr: {
    ip: ipFromString(ip),
    port,
  },
  proto,
})

var links = {}
var balancers = {}

var upstreams = (function() {
  var mapByID = {}
  var mapByEP = {}
  var idNext = 1
  var idPool = new algo.ResourcePool(() => idNext++)

  function make(ip, port) {
    var ep = epFromIPPort(ip, port)
    if (ep in mapByEP) return mapByEP[ep]
    var id = idPool.allocate()
    maps.upstreams.update({ i: id }, {
      addr: {
        ip: ipFromString(ip),
        port,
      },
    })
    return mapByID[id] = { ip, port, id }
  }

  function cleanup(upstreams) {
    Object.keys(mapByID).forEach(
      id => {
        if (id in upstreams) return
        delete mapByID[id]
        delete mapByEP[epFromUpstream[upstreams[id]]]
        idPool.free(id)
        maps.upstreams.delete({ id })
      }
    )
  }

  return { make, cleanup }
})()

setupBalancers()

rtnl(
  function (type, obj) {
    switch (type) {
      case 'RTM_NEWLINK': newLink(obj); break
      case 'RTM_DELLINK': delLink(obj); break
      case 'RTM_NEWADDR': newAddress(obj); break
      case 'RTM_DELADDR': delAddress(obj); break
      case 'RTM_NEWROUTE': newRoute(obj); break
      case 'RTM_DELROUTE': delRoute(obj); break
      case 'RTM_NEWNEIGH': newNeighbour(obj); break
      case 'RTM_DELNEIGH': delNeighbour(obj); break
    }
  }
)

watchConfig()

function watchConfig() {
  pipy.watch('config.yml', () => {
    setupBalancers()
    watchConfig()
  })
}

pipy.exit(function () {
  Object.keys(links).forEach(
    index => setLinkXDP(index, -1)
  )
})

function setupBalancers() {
  var newBalancers = {}
  var newUpstreams = {}
  console.log('Setting up balancers...')
  YAML.decode(pipy.load('config.yml')).balancers.forEach(
    function ({ ip, port, targets }) {
      var balancer = { ip, port }
      var ring = new algo.LoadBalancer(
        targets.map(
          function ({ ip, port, weight }) {
            var upstream = upstreams.make(ip, port)
            newUpstreams[upstream.id] = upstream
            return { upstream, weight }
          }, {
            weight: t => t.weight
          }
        )
      ).schedule(RING_SIZE).map(t => t.upstream.id)
      console.log('  Update balancer', ip, port)
      newBalancers[epFromIPPort(ip, port)] = balancer
      maps.balancers.update(
        balancerKey(ip, port, 6), { ring }
      )
    }
  )
  Object.entries(balancers).forEach(
    function ([k, v]) {
      if (k in newBalancers) return
      maps.balancers.delete(balancerKey(v.ip, v.port, 6))
      console.log('  Delete balancer', v.ip, v.port)
    }
  )
  upstreams.cleanup(newUpstreams)
  balancers = newBalancers
  console.log('Balancers updated')
}

function initLink(index) {
  if (index in links) return links[index]
  setLinkXDP(index, program.fd)
  return links[index] = {}
}

function newLink(link) {
  console.log('new link:', 'address', link.address, 'broadcast', link.broadcast, 'ifname', link.ifname, 'index', link.index)
  maps.links.update({ i: link.index }, {
    ...initLink(link.index),
    mac: macFromString(link.address),
  })
}

function delLink(link) {
  console.log('del link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index)
  maps.links.delete({ i: link.index })
}

function newAddress(addr) {
  console.log('add addr:', 'address', addr.address, 'prefixlen', addr.prefixlen, 'index', addr.index)
  new Netmask(addr.address).version === 4 && (
    maps.links.update(
      { i: addr.index },
      Object.assign(initLink(addr.index), { ip: ipFromString(addr.address) }),
    )
  )
}

function delAddress(addr) {
  console.log('del addr:', 'address', addr.address, 'prefixlen', addr.prefixlen, 'index', addr.index)
}

function newRoute(route) {
  console.log('new route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif, 'gateway', route.gateway)
  route.gateway && maps.routes.update(routeKey(route), ipFromString(route.gateway))
}

function delRoute(route) {
  console.log('del route:', 'dst', route.dst, 'dst_len', route.dst_len, 'oif', route.oif)
  maps.routes.delete(routeKey(route))
}

function newNeighbour(neigh) {
  console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr, 'ifindex', neigh.ifindex)
  maps.neighbours.update(
    ipFromString(neigh.dst), {
      interface: neigh.ifindex,
      mac: macFromString(neigh.lladdr),
    }
  )
}

function delNeighbour(neigh) {
  console.log('new neigh:', 'dst', neigh.dst, 'lladdr', neigh.lladdr, 'ifindex', neigh.ifindex)
  maps.neighbours.delete(ipFromString(neigh.dst))
}
