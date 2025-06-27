export default function ({ log }) {
  var pendingMsgs = {}
  var numConnections = 0

  var $sessionCtrl

  pipy.listen(8080, $=>$
    .onStart(() => {
      numConnections++
      $sessionCtrl = {}
    })
    .split('\n')
    .replaceMessage(msg => {
      if (msg.body?.size > 0) {
        return msg
      }
    })
    .demux().to($=>$
      .replaceMessage(
        msg => {
          var data = JSON.decode(msg.body)
          var id = data.id
          pendingMsgs[id] = true
          return new Timeout(Math.random() * 5).wait().then(
            () => {
              if (!$sessionCtrl.aborted) {
                delete pendingMsgs[id]
                data.result = id
                return new Message(JSON.encode(data))
              }
            }
          )
        }
      )
    )
    .replaceMessage(
      msg => msg.body.push('\n')
    )
    .insert(
      () => new Timeout(Math.random() * 2 + 5).wait().then(
        () => {
          $sessionCtrl.aborted = true
          return new StreamEnd
        }
      )
    )
  )

  var messageID = 0
  var sessionIndex = 0
  var results = []

  var $msg

  var send = pipeline($=>$
    .onStart(() => {
      results.push($msg = {
        id: ++messageID,
        content: new Array(Math.floor(Math.random() * 5000)).fill().map(
          () => String.fromCharCode(Math.random() * 0x60 + 0x20)
        ).join(''),
      })
      return new Message(JSON.encode($msg))
    })
    .muxQueue(() => sessionIndex++ % 10, { maxSessions: 1 }).to($=>$
      .replaceMessage(msg => msg.body.push('\n'))
      .connect('localhost:8000')
      .split('\n')
    )
    .handleStreamEnd(
      eos => {
        $msg.eos = eos
      }
    )
    .replaceMessage(
      msg => {
        try {
          $msg.response = JSON.decode(msg.body)
        } catch {}
        return new StreamEnd
      }
    )
  )

  var counter = 0

  function run() {
    return new Timeout(0.01).wait().then(() => {
      if (counter++ < 1000) {
        send.spawn()
        return run()
      }
    })
  }

  return run().then(
    () => new Timeout(10).wait()
  ).then(
    () => {
      var ok = true
      log('Upstream accepted', numConnections, 'connections')
      results.forEach(r => {
        if (r.id === r.response?.result) return
        if (!r.response) {
          if (r.id in pendingMsgs) {
            delete pendingMsgs[r.id]
            return
          }
          log('Lost message ID =', r.id)
          ok = false
        } else {
          log('Req/Res mismatch:', r.id, '->', r.response.result)
          ok = false
        }
      })
      return ok
    }
  )
}
