#!/usr/bin/env pipy

var argv = (function () {
  var seen = false
  return pipy.argv.filter(opt => {
    if (seen) return true;
    if (opt === '--') seen = true;
    return false;
  })
})()

var mode = argv[0]
var config = YAML.decode(pipy.load('config.yml'))

if (mode == '--server') {
  console.info('Started in server mode')
  runServer()
} else if (mode == '--client') {
  console.info('Started in client mode')
  runClient()
} else {
  console.error('Missing option --server or --client')
}

//
// Run as server
//

function runServer() {
  var $target
  var $source

  var hubs = {}

  var ok = new Message({
    status: 101,
    headers: {
      connection: 'upgrade',
      upgrade: 'websocket',
    }
  })

  var deny = new Message({
    status: 404
  })

  var send = pipeline($=>$
    .connect(() => $target, { protocol: 'udp' })
  )

  var recv = pipeline($=>$
    .swap(() => hubs[$source])
  )

  pipy.listen(config.server.listen, $=>$
    .demuxHTTP().to($=>$
      .acceptHTTPTunnel(
        function (req) {
          var path = req.head.path
          if (path.startsWith('/send/')) {
            $target = path.substring(6)
            console.info(`Started forwarding to ${$target}`)
            return ok
          } else if (path.startsWith('/recv/')) {
            $source = path.substring(6)
            hubs[$source] ??= new pipeline.Hub
            console.info(`Started receiving from ${$source}`)
            return ok
          } else {
            return deny
          }
        }
      ).to($=>$
        .decodeWebSocket()
        .pipe(() => $target ? send : recv)
        .replaceData(data => new Message(data))
        .encodeWebSocket()
      )
    )
  )

  config.server.forwarding.forEach(
    function ({ listen, target }) {
      pipy.listen(listen, 'udp', $=>$
        .swap(() => hubs[target])
      )
    }
  )
}

//
// Run as client
//

function runClient() {
  config.client.forwarding.forEach(
    function ({ listen, target }) {
      pipy.listen(listen, 'udp', $=>$
        .replaceData(data => new Message(data))
        .encodeWebSocket()
        .connectHTTPTunnel(
          new Message({
            path: `/send/${target}`,
            headers: {
              connection: 'upgrade',
              upgrade: 'websocket',
            }
          })
        ).to($=>$
          .muxHTTP().to($=>$
            .connect(config.client.connect)
          )
        )
        .decodeWebSocket()
      )
    }
  )

  config.server.forwarding.forEach(
    function ({ target }) {
      pipeline($=>$
        .onStart(new Data)
        .replay({ delay: 1 }).to($=>$
          .loop($=>$
            .replaceData(data => new Message(data))
            .encodeWebSocket()
            .connectHTTPTunnel(
              new Message({
                path: `/recv/${target}`,
                headers: {
                  connection: 'upgrade',
                  upgrade: 'websocket',
                }
              })
            ).to($=>$
              .muxHTTP().to($=>$
                .connect(config.client.connect)
              )
            )
            .decodeWebSocket()
            .connect(target, { protocol: 'udp' })
          )
          .replaceStreamEnd(new StreamEnd('Replay'))
        )
      ).spawn()
    }
  )
}
