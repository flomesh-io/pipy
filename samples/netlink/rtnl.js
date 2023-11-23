((
  // struct rtattr (linux/rtnetlink.h)
  rtattr = new CStruct()
    .field('uint16', 'len')
    .field('uint16', 'type'),

  // struct ifinfomsg (linux/rtnetlink.h)
  ifinfomsg = new CStruct()
    .field('uint8', 'family')
    .field('uint8', 'pad')
    .field('uint16', 'type')
    .field('uint32', 'index')
    .field('uint32', 'flags')
    .field('uint32', 'change'),

  // struct ifaddrmsg (linux/ifaddr.h)
  ifaddrmsg = new CStruct()
    .field('uint8', 'family')
    .field('uint8', 'prefixlen')
    .field('uint8', 'flags')
    .field('uint8', 'scope')
    .field('uint32', 'index'),

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

}))()
