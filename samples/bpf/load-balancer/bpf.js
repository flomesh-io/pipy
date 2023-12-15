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
    neighbor: 'uint32',
  }),

  Neighbor = new CStruct({
    mac: `uint8[${ETH_ALEN}]`,
    interface: 'uint32',
  }),

  map = (name, key, value) => (
    bpf.Map.open(
      bpf.Map.list().find(i => i.name === name)?.id,
      key, value
    )
  ),

) => ({
  maps: {
    neighbors: map('map_neighbors', ID, Neighbor),
    upstreams: map('map_upstreams', ID, Upstream),
    balancers: map('map_balancers', Endpoint, Balancer),
  },
})

)()
