((
  bpf = pipy.solve('bpf.js'),
) => (

bpf.maps.neighbors.update(
  { id: 0 },
  { mac: [0, 0, 0, 0, 0, 0] },
),

bpf.maps.upstreams.update(
  { id: 0 },
  { addr: { ip: { v4: [127, 0, 0, 1] }, port: 8080 }, neighbor: 0 },
),

bpf.maps.balancers.update(
  { addr: { ip: { v4: [127, 0, 0, 1] }, port: 8000 }, proto: 6 },
  { ring: [0], hint: 0 },
),

pipy()

.listen(8080)
.serveHTTP(new Message('hi'))

))()
