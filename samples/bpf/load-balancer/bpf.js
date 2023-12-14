((
  ETH_ALEN = 6,
  RING_SIZE = 63356,

  ID = new CStruct()
    .field('uint32', 'id'),

  IP = new CStruct()
    .field('uint8[4]', 'v4'),

  Address = new CStruct()
    .field(IP, 'ip')
    .field('uint16', 'port'),

  Endpoint = new CStruct()
    .field(Address, 'addr')
    .field('uint8', 'proto'),

  Balancer = new CStruct()
    .field(`uint32[${RING_SIZE}]`, 'ring')
    .field('uint32', 'hint'),

  Upstream = new CStruct()
    .field(Address, 'addr')
    .field('uint32', 'neighbor'),

  Neighbor = new CStruct()
    .field(`uint8[${ETH_ALEN}]`, 'mac')
    .field('uint32', 'interface'),

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
