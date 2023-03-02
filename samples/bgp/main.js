((
  config = JSON.decode(pipy.load('config.json')),
  newPeer = pipy.solve('peer.js'),
  allPeers = config.peers.map(addr => newPeer(config, addr))

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
    console.info('config.json updated to', update),
    config.reachable = update.reachable,
    config.unreachable = update.unreachable,
    allPeers.forEach(peer => peer.update()),
    new StreamEnd
  )
)())

)()
