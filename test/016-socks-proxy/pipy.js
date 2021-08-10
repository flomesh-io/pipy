pipy({
  _addr: '',
  _port: 0,
})

.listen(6080)
  .proxySOCKS(
    'connect',
    (addr, port) => (
      _addr = addr,
      _port = port,
      true // return true to accept the connection
    )
  )

.pipeline('connect')
  .connect(
    () => `${_addr}:${_port}`
  )