//
// Protected backend service
//

pipy.listen(8080, $=>$
  .serveHTTP(
    new Message('hi!\n')
  )
)

//
// Rate-limiting proxy
//

var $path

pipy.listen(8000, $=>$
  .demuxHTTP().to($=>$
    .handleMessageStart(msg => $path = msg.head.path)
    .forkJoin([1]).to($=>$
      .onStart(() => new Message({ path: $path }))
      .muxHTTP({ version: 2 }).to($=>$
        .connect('localhost:8001')
      )
      .replaceMessage(new StreamEnd)
    )
    .muxHTTP().to($=>$
      .connect('localhost:8080')
    )
  )
)

//
// Global rate-limiting thread
//

if (pipy.thread.id === 0) {
  var limits = YAML.decode(pipy.load('config.yaml')).limits
  var router = new algo.URLRouter(
    Object.fromEntries(
      limits.map(
        function ({ path, rate }) {
          var n = Number.parseFloat(rate)
          if (n > 0) return [path, new algo.Quota(n, { per: 1 })]
          else return [path, null]
        }
      )
    )
  )

  var $quota

  var pass = pipeline($=>$)
  var throttled = pipeline($=>$.throttleMessageRate(() => $quota))

  pipy.listen(8001, $=>$
    .demuxHTTP().to($=>$
      .pipe(
        function (evt) {
          if (evt instanceof MessageStart) {
            $quota = router.find(evt.head.path)
            return $quota ? throttled : pass
          }
        }
      )
      .replaceMessage(new Message)
    )
  )
}
