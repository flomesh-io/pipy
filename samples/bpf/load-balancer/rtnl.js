import queue from './queue.js'

var i32Struct = new CStruct({ i: 'int32' })

// struct rtattr (linux/rtnetlink.h)
var rtattr = new CStruct({
  len: 'uint16',
  type: 'uint16',
})

// struct ifinfomsg (linux/rtnetlink.h)
var ifinfomsg = new CStruct({
  family: 'uint8',
  pad: 'uint8',
  type: 'uint16',
  index: 'uint32',
  flags: 'uint32',
  change: 'uint32',
})

// struct ifaddrmsg (linux/ifaddr.h)
var ifaddrmsg = new CStruct({
  family: 'uint8',
  prefixlen: 'uint8',
  flags: 'uint8',
  scope: 'uint8',
  index: 'uint32',
})

// struct rtmsg (linux/rtnetlink.h)
var rtmsg = new CStruct({
  family: 'uint8',
  dst_len: 'uint8',
  src_len: 'uint8',
  tos: 'uint8',
  table: 'uint8',
  protocol: 'uint8',
  scope: 'uint8',
  type: 'uint8',
  flags: 'uint32',
})

// struct ndmsg (linux/neighbour.h)
var ndmsg = new CStruct({
  family: 'uint8',
  pad1: 'uint8',
  pad2: 'uint16',
  ifindex: 'int32',
  state: 'uint16',
  flags: 'uint8',
  type: 'uint8',
})

var netlinkRequests = queue()

function align(size) {
  return (size + 3) & ~3
}

function decodeAttrs(data) {
  var attrs = {}
  return repeat(() => {
    var hdr = rtattr.decode(data.shift(rtattr.size))
    attrs[hdr.type] = data.shift(align(hdr.len) - 4).shift(hdr.len - 4)
    return data.size > rtattr.size ? undefined : attrs
  })
}

function encodeAttrs(attrs) {
  var buffer = new Data
  if (attrs) {
    Object.entries(attrs).forEach(
      function ([k, v]) {
        var data = v instanceof Data ? v : encodeAttrs(v)
        var size = data.size
        buffer.push(rtattr.encode({ len: size + rtattr.size, type: k|0 }))
        buffer.push(data)
        buffer.push(new Data(Array(align(size) - size).fill(0)))
      }
    )
  }
  return buffer
}

function appendAttrs(data, attrs) {
  data.push(encodeAttrs(attrs))
  return data
}

function i8(data) {
  if (data) {
    return data.toArray()[0]
  }
}

function i32(data) {
  if (data) {
    return data.toArray().reduce(
      (a, b, i) => (b << (i*8)) | a
    )
  }
}

function hex(data) {
  if (data) {
    return data.toArray().map(
      n => n.toString(16).padStart(2, '0')
    ).join(':')
  }
}

function ip(data) {
  if (data) {
    return new Netmask(
      data.length > 4 ? 128 : 32,
      data.toArray()
    ).ip
  }
}

function encodeLink(msg) {
  return appendAttrs(ifinfomsg.encode(msg), msg.attrs)
}

function decodeLink(data) {
  var info = ifinfomsg.decode(data.shift(ifinfomsg.size))
  var attrs = decodeAttrs(data)
  return {
    address: hex(attrs[1]), // IFLA_ADDRESS
    broadcast: hex(attrs[2]), // IFLA_BROADCAST
    ifname: attrs[3]?.toString(), // IFLA_IFNAME
    mtu: i32(attrs[4]), // IFLA_MTU
    master: i32(attrs[10]), // IFLA_MASTER
    operstate: i8(attrs[16]), // IFLA_OPERSTATE
    ...info,
  }
}

function encodeAddr(msg) {
  return appendAttrs(ifaddrmsg.encode(msg, msg.attrs))
}

function decodeAddr(data) {
  var info = ifaddrmsg.decode(data.shift(ifaddrmsg.size))
  var attrs = decodeAttrs(data)
  return {
    address: ip(attrs[1]), // IFA_ADDRESS
    local: ip[attrs[2]], // IFA_LOCAL
    broadcast: ip[attrs[4]], // IFA_BROADCAST
    ...info,
  }
}

function encodeRoute(msg) {
  return appendAttrs(rtmsg.encode(msg, msg.attrs))
}

function decodeRoute(data) {
  var info = rtmsg.decode(data.shift(rtmsg.size))
  var attrs = decodeAttrs(data)
  return {
    dst: ip(attrs[1]), // RTA_DST
    oif: i32(attrs[4]), // RTA_OIF
    gateway: ip(attrs[5]), // RTA_GATEWAY
    via: ip(attrs[18]), // RTA_VIA
    ...info,
  }
}

function encodeNeigh(msg) {
  return appendAttrs(ndmsg.encode(msg, msg.attrs))
}

function decodeNeigh(data) {
  var info = ndmsg.decode(data.shift(ndmsg.size))
  var attrs = decodeAttrs(data)
  return {
    dst: ip(attrs[1]), // NDA_DST
    lladdr: hex(attrs[2]), // NDA_LLADDR
    ifindex: i32(attrs[8]), // NDA_IFINDEX
    ...info,
  }
}

export default function(cb) {
  var query = pipeline($=>$
    .onStart(msg => msg)
    .encodeNetlink()
    .connect('pid=0;groups=0', {
      protocol: 'netlink',
      netlinkFamily: 0,
    })
    .decodeNetlink()
    .handleMessage(handle)
  )

  var watch = pipeline($=>$
    .onStart(new Data)
    .connect('pid=0;groups=0', {
      protocol: 'netlink',
      netlinkFamily: 0,
      bind: `pid=0;groups=${1|4|0x40|0x400}`, // groups = RTMGRP_LINK | RTMGRP_NEIGH | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE
    })
    .decodeNetlink()
    .handleMessage(handle)
  )

  var flags = 0x01 | 0x0100 | 0x0200 // NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH

  query.spawn(new Message({ type: 18, flags }, encodeLink({}))) // RTM_GETLINK
  query.spawn(new Message({ type: 22, flags }, encodeAddr({}))) // RTM_GETADDR
  query.spawn(new Message({ type: 26, flags }, encodeRoute({}))) // RTM_GETROUTE
  query.spawn(new Message({ type: 30, flags }, encodeNeigh({}))) // RTM_GETNEIGH

  watch.spawn()

  function handle(msg) {
    switch (msg.head.type) {
      case 16: cb('RTM_NEWLINK', decodeLink(msg.body)); break
      case 17: cb('RTM_DELLINK', decodeLink(msg.body)); break
      case 20: cb('RTM_NEWADDR', decodeAddr(msg.body)); break
      case 21: cb('RTM_DELADDR', decodeAddr(msg.body)); break
      case 24: cb('RTM_NEWROUTE', decodeRoute(msg.body)); break
      case 25: cb('RTM_DELROUTE', decodeRoute(msg.body)); break
      case 28: cb('RTM_NEWNEIGH', decodeNeigh(msg.body)); break
      case 29: cb('RTM_DELNEIGH', decodeNeigh(msg.body)); break
    }
  }
}

export function setLinkXDP(index, fd) {
  netlinkRequests.enqueue(
    new Message(
      {
        type: 19, // RTM_SETLINK
        flags: 0x01 | 0x04, // NLM_F_REQUEST | NLM_F_ACK
      },
      encodeLink({
        index,
        attrs: {
          [43]: { // IFLA_XDP
            [1]: i32Struct.encode({ i: fd }), // IFLA_XDP_FD
            [3]: i32Struct.encode({ i: 1 << 1 }), // IFLA_XDP_FLAGS: XDP_FLAGS_SKB_MODE
          },
        }
      })
    )
  )
}

pipeline($=>$
  .encodeNetlink()
  .connect('pid=0;groups=0', {
    protocol: 'netlink',
    netlinkFamily: 0,
  })
).process(
  () => netlinkRequests.dequeue()
)
