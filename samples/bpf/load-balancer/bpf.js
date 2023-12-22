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

  Link = new CStruct({
    mac: `uint8[${ETH_ALEN}]`,
    ip: IP,
  }),

  Neighbour = new CStruct({
    interface: 'uint32',
    mac: `uint8[${ETH_ALEN}]`,
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
    links: map('map_links', ID, Link),
    neighbours: map('map_neighbours', IP, Neighbour),
    routes: map('map_routes', IPMask, IP),
    upstreams: map('map_upstreams', ID, Upstream),
    balancers: map('map_balancers', Endpoint, Balancer),
  },
})

)()
