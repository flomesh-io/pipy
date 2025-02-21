import newPeer from './peer.js'

var CONFIG_FILENAME = 'config.yaml'
var config = YAML.decode(pipy.load(CONFIG_FILENAME))
var allPeers = config.peers.map(addr => newPeer(config, addr))
var isExiting = false

var $peer

var session = pipeline($=>$
  .mux(() => $peer, { maxQueue: 0 }).to($=>$
    .encodeBGP({ enableAS4: () => $peer.isAS4() })
    .handleMessage(msg => console.debug('>>>', $peer.state(), msg.payload))
    .connect(() => $peer.destination, { idleTimeout: 0 })
    .decodeBGP({ enableAS4: () => $peer.isAS4() })
    .handleMessage(msg => console.debug('<<<', $peer.state(), msg.payload))
    .handleMessage(msg => $peer.receive(msg))
    .handleStreamEnd(() => $peer.reset())
  )
)

var tick = pipeline($=>$
  .onStart(new Message)
  .fork(allPeers).to($=>$
    .onStart(p => void ($peer = p))
    .replaceMessage(() => $peer.tick())
    .demux().to(session)
  )
  .replaceMessage(new StreamEnd)
)

// Spawn a pipeline every second to keep ticking
scheduleTick()
function scheduleTick() {
  tick.spawn()
  new Timeout(1).wait().then(scheduleTick)
}

// Reload routing info when config.yaml is changed
watchConfig()
function watchConfig() {
  pipy.watch(CONFIG_FILENAME).then(() => {
    if (!isExiting) {
      var update = YAML.decode(pipy.load(CONFIG_FILENAME))
      console.info('config.json updated to', update)
      config.ipv4 = update.ipv4
      config.ipv6 = update.ipv6
      allPeers.forEach(peer => peer.update())
    }
    watchConfig()
  })
}

// Mark all routes as unreachable when exit
pipy.exit(
  function () {
    isExiting = true
    console.info('Updating all routes as unreachable as BGP speaker exits...')
    if (config.ipv4) {
      config.ipv4.unreachable = [
        ...(config.ipv4.unreachable || []),
        ...(config.ipv4.reachable || []),
      ]
      config.ipv4.reachable = []
    }
    if (config.ipv6) {
      config.ipv6.unreachable = [
        ...(config.ipv6.unreachable || []),
        ...(config.ipv6.reachable || []),
      ]
      config.ipv6.reachable = []
    }
    allPeers.forEach(peer => peer.update())
    return new Timeout(10).wait().then(
      () => console.info('Done.')
    )
  }
)
