#!/usr/bin/env pipy

import rtnl from './rtnl.js'

var PIN_PATH = '/sys/fs/bpf/port-interceptor'

var obj = bpf.object(pipy.load('port-interceptor.o'))
var prog = obj.programs[0].load('BPF_PROG_TYPE_SCHED_CLS')
var maps = Object.fromEntries(obj.maps.map(m => [m.name, m]))
var dnat = maps['map_dnat']
var snat = maps['map_snat']

var allLinks = {}
var portMapping = {}

updateBPFMaps()
bpf.pin(PIN_PATH, prog.fd)

rtnl(
  function (type, obj) {
    switch (type) {
      case 'RTM_NEWLINK': newLink(obj); break
      case 'RTM_DELLINK': delLink(obj); break
    }
  }
)

watchConfig()

function watchConfig() {
  pipy.watch('config.yaml').then(() => {
    updateBPFMaps()
    watchConfig()
  })
}

pipy.exit(function () {
  Object.keys(allLinks).forEach(
    function (dev) {
      console.info(`Cleanning up BPF filters on dev ${dev}...`)
      pipy.exec(`tc filter del dev ${dev} egress`)
      pipy.exec(`tc filter del dev ${dev} ingress`)
      pipy.exec(`tc qdisc del dev ${dev} clsact`)
      pipy.exec(`rm ${PIN_PATH}`)
    }
  )
})

function newLink(link) {
  var isLoopback = Boolean(link.flags & (1<<3))
  console.info('new link:', 'index', link.index, 'ifname', link.ifname, 'address', link.address, 'loopback', isLoopback)
  if (!isLoopback) {
    var dev = link.ifname
    console.info(`Hooking up BPF filters on dev ${dev}...`)
    pipy.exec(`tc qdisc add dev ${dev} clsact`)
    pipy.exec(`tc filter add dev ${dev} egress bpf direct-action object-pinned ${PIN_PATH}`)
    pipy.exec(`tc filter add dev ${dev} ingress bpf direct-action object-pinned ${PIN_PATH}`)
    allLinks[dev] = link
  }
}

function delLink(link) {
  console.info('del link:', 'address', link.address, 'ifname', link.ifname, 'index', link.index)
  delete allLinks[link.ifname]
}

function updateBPFMaps() {
  console.info('Updating BPF maps...')

  var newPortMapping = {}
  YAML.decode(pipy.load('config.yaml')).interceptors.forEach(
    ({ port, originalPort }) => newPortMapping[originalPort] = port
  )

  // Delete old mappings
  Object.entries(portMapping).forEach(
    function ([k, v]) {
      if (portMapping[k] === newPortMapping[k]) return
      dnat.delete({ i: k })
      snat.delete({ i: v })
      console.info(`  Deleted port mapping ${k} <---> ${v}`)
    }
  )

  // Update new mappings
  Object.entries(newPortMapping).forEach(
    function ([k, v]) {
      dnat.update({ i: k }, { i: v })
      snat.update({ i: v }, { i: k })
      console.info(`  Created port mapping ${k} <---> ${v}`)
    }
  )

  portMapping = newPortMapping
}
