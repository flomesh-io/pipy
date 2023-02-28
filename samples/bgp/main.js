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
  .mux(() => _peer).to(
    $=>$
    .encodeBGP()
    .handleMessage(msg => console.debug('>>>', _peer.state(), msg.payload))
    .connect(() => _peer.destination, { idleTimeout: 0 })
    .decodeBGP()
    .handleMessage(msg => console.debug('<<<', _peer.state(), msg.payload))
    .handleMessage(msg => _peer.receive(msg))
    .handleStreamEnd(() => _peer.end())
  )
)

.watch('config.json')
.onStart(() => (
  (
    update = JSON.decode(pipy.load('config.json')),
  ) => (
    console.info('config.json updated to', update),
    config.prefixes = update.prefixes,
    allPeers.forEach(peer => peer.update()),
    new StreamEnd
  )
)())

)()
