((
  config = JSON.decode(pipy.load('config.json')),
  newPeer = pipy.solve('peer.js'),
  allPeers = config.peers.map(addr => newPeer(config, addr)),
  isExiting = false,

) => pipy({
  _peer: null,
})

.task('1s')
.onStart(() => [new Message, new StreamEnd])
.fork(allPeers).to(
  $=>$
  .onStart(peer => void (_peer = peer))
  .replaceMessage(() => _peer.tick())
  .replaceStreamEnd()
  .demux().to(
    $=>$.mux(() => _peer).to(
      $=>$
      .encodeBGP({ enableAS4: () => _peer.isAS4() })
      .handleMessage(msg => console.debug('>>>', _peer.state(), msg.payload))
      .connect(() => _peer.destination, { idleTimeout: 0 })
      .decodeBGP({ enableAS4: () => _peer.isAS4() })
      .handleMessage(msg => console.debug('<<<', _peer.state(), msg.payload))
      .handleMessage(msg => _peer.receive(msg))
      .handleStreamEnd(() => _peer.end())
    )
  )
)

.watch('config.json')
.onStart(() => (
  (
    update = JSON.decode(pipy.load('config.json')),
  ) => (
    isExiting || (
      console.info('config.json updated to', update),
      config.ipv4 = update.ipv4,
      config.ipv6 = update.ipv6,
      allPeers.forEach(peer => peer.update())
    ),
    new StreamEnd
  )
)())

.exit()
.onStart(() => (
  isExiting = true,
  console.info('Updating all routes as unreachable as BGP speaker exits...'),
  config.ipv4 && (
    config.ipv4.unreachable = [
      ...(config.ipv4.unreachable || []),
      ...(config.ipv4.reachable || []),
    ],
    config.ipv4.reachable = []
  ),
  config.ipv6 && (
    config.ipv6.unreachable = [
      ...(config.ipv6.unreachable || []),
      ...(config.ipv6.reachable || []),
    ],
    config.ipv6.reachable = []
  ),
  allPeers.forEach(peer => peer.update()),
  new StreamEnd
))
.wait(() => new Timeout(10).wait())
.handleStreamEnd(() => console.info('Done.'))

)()
