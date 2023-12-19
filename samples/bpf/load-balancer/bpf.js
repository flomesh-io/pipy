((
  ETH_ALEN = 6,
  RING_SIZE = 16,

  ID = new CStruct({
    id: 'uint32',
  }),

  IP = new CStruct({
    v4: 'uint8[4]',
  }),

  IPMask = new CStruct({
    mask: new CStruct({
      prefixlen: 'uint32',
    }),
    ip: IP,
  }),

  Route = new CStruct({
    interface: 'uint32',
    broadcast: `uint8[${ETH_ALEN}]`,
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
  }),

  map = (name, key, value) => (
    bpf.Map.open(
      bpf.Map.list().find(i => i.name === name)?.id,
      key, value
    )
  ),

) => ({
  IPMask,
  maps: {
    routes: map('map_routes', IPMask, Route),
    upstreams: map('map_upstreams', ID, Upstream),
    balancers: map('map_balancers', Endpoint, Balancer),
  },
})

)()
