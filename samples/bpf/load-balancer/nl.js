((
  // struct rtattr (linux/rtnetlink.h)
  rtattr = new CStruct({
    len: 'uint16',
    type: 'uint16',
  }),

  // struct ifinfomsg (linux/rtnetlink.h)
  ifinfomsg = new CStruct({
    family: 'uint8',
    pad: 'uint8',
    type: 'uint16',
    index: 'uint32',
    flags: 'uint32',
    change: 'uint32',
  }),

  // struct ifaddrmsg (linux/ifaddr.h)
  ifaddrmsg = new CStruct({
    family: 'uint8',
    prefixlen: 'uint8',
    flags: 'uint8',
    scope: 'uint8',
    index: 'uint32',
  }),

  // struct rtmsg (linux/rtnetlink.h)
  rtmsg = new CStruct({
    family: 'uint8',
    dst_len: 'uint8',
    src_len: 'uint8',
    tos: 'uint8',
    table: 'uint8',
    protocol: 'uint8',
    scope: 'uint8',
    type: 'uint8',
    flags: 'uint32',
  }),

  // struct ndmsg (linux/neighbour.h)
  ndmsg = new CStruct({
    family: 'uint8',
    pad1: 'uint8',
    pad2: 'uint16',
    ifindex: 'int32',
    state: 'uint16',
    flags: 'uint8',
    type: 'uint8',
  }),

  extractAttrs = data => (
    (
      attrs = {},
      hdr,
    ) => (
      repeat(() => (
        hdr = rtattr.decode(data.shift(rtattr.size)),
        attrs[hdr.type] = data.shift(((hdr.len + 3) & ~3) - 4).shift(hdr.len - 4),
        data.size > rtattr.size ? undefined : attrs
      ))
    )
  )(),

  appendAttrs = (data, attrs) => (
    attrs ? Object.entries(attrs)?.reduce(
      (data, [k, v]) => (
        (
          size = (v.size + 3) & ~3,
        ) => (data
          .push(rtattr.encode({ len: size + rtattr.size, type: k|0 }))
          .push(v)
          .push(new Data(new Array(size - v.size).fill(0)))
        )
      )(), data
    ) : data
  ),

  i8 = data => (
    !data ? undefined : (
      data.toArray()[0]
    )
  ),

  i32 = data => (
    !data ? undefined : (
      data.toArray().reduce(
        (a, b, i) => (a + (b << (i * 8)))
      )
    )
  ),

  hex = data => (
    !data ? undefined : (
      data.toArray().map(
        n => n.toString(16).padStart(2, '0')
      ).join(':')
    )
  ),

  ip = data => (
    !data ? undefined : (
      new Netmask(data.length > 4 ? 128 : 32, data.toArray()).ip
    )
  ),

) => ({

  link: {
    encode: msg => appendAttrs(ifinfomsg.encode(msg), msg.attrs),
    decode: data => (
      (
        info = ifinfomsg.decode(data.shift(ifinfomsg.size)),
        attrs = extractAttrs(data),
      ) => ({
        address: hex(attrs[1]), // IFLA_ADDRESS
        broadcast: hex(attrs[2]), // IFLA_BROADCAST
        ifname: attrs[3]?.toString(), // IFLA_IFNAME
        mtu: i32(attrs[4]), // IFLA_MTU
        master: i32(attrs[10]), // IFLA_MASTER
        operstate: i8(attrs[16]), // IFLA_OPERSTATE
        ...info,
      })
    )(),
  },

  addr: {
    encode: msg => appendAttrs(ifaddrmsg.encode(msg, msg.attrs)),
    decode: data => (
      (
        info = ifaddrmsg.decode(data.shift(ifaddrmsg.size)),
        attrs = extractAttrs(data),
      ) => ({
        address: ip(attrs[1]), // IFA_ADDRESS
        local: ip[attrs[2]], // IFA_LOCAL
        broadcast: ip[attrs[4]], // IFA_BROADCAST
        ...info,
      })
    )(),
  },

  route: {
    encode: msg => appendAttrs(rtmsg.encode(msg, msg.attrs)),
    decode: data => (
      (
        info = rtmsg.decode(data.shift(rtmsg.size)),
        attrs = extractAttrs(data),
      ) => ({
        dst: ip(attrs[1]), // RTA_DST
        oif: i32(attrs[4]), // RTA_OIF
        gateway: ip(attrs[5]), // RTA_GATEWAY
        via: ip(attrs[18]), // RTA_VIA
        ...info,
      })
    )(),
  },

  neigh: {
    encode: msg => appendAttrs(ndmsg.encode(msg, msg.attrs)),
    decode: data => (
      (
        info = ndmsg.decode(data.shift(ndmsg.size)),
        attrs = extractAttrs(data),
      ) => ({
        dst: ip(attrs[1]), // NDA_DST
        lladdr: hex(attrs[2]), // NDA_LLADDR
        ifindex: i32(attrs[8]), // NDA_IFINDEX
        ...info,
      })
    )(),
  },

}))()
