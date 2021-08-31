pipy({
  _WHITELIST: [
    // IPs or IP masks to let through
    // '127.0.0.0/24',
  ].map(cidr => new Netmask(cidr)),

  _BLACKLIST: [
    // IPs or IP masks to block
    // '127.0.0.0/24',
  ].map(cidr => new Netmask(cidr)),

  _addr: '',
  _port: 0,
})

.listen(6080)
  .proxySOCKS(
    'connect',
    (addr, port) => (
      _addr = addr,
      _port = port,

      // return true to accept the connection or false to refuse
      _WHITELIST.length > 0 ? (
        _WHITELIST.some(mask => mask.contains(addr))
      ) : (
        !_BLACKLIST.some(mask => mask.contains(addr))
      )
    )
  )

.pipeline('connect')
  .connect(
    () => `${_addr}:${_port}`
  )