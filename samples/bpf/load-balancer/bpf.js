((
  ETH_ALEN = 6,
  RING_SIZE = 63356,

  ID = new CStruct({
    id: 'uint32',
  }),

  IP = new CStruct({
    v4: 'uint8[4]',
  }),

  Address = new CStruct({
    ip: IP,
    port: 'uint16',
  }),

  Endpoint = new CStruct({
    addr: Address,
    proto: 'uint8',
  }),

  Balancer = new CStruct({
    ring: `uint32[${RING_SIZE}]`,
    hint: 'uint32',
  }),

  Upstream = new CStruct({
    addr: Address,
    interface: 'uint32',
    mac: `uint8[${ETH_ALEN}]`,
  }),

  map = (name, key, value) => (
    bpf.Map.open(
      bpf.Map.list().find(i => i.name === name)?.id,
      key, value
    )
  ),

) => ({
  maps: {
    upstreams: map('map_upstreams', ID, Upstream),
    balancers: map('map_balancers', Endpoint, Balancer),
  },
})

)()
