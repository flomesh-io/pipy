import config from '/config.js'

var balancers = Object.fromEntries(
  Object.entries(config.upstreams).map(
    ([name, { targets, algorithm }]) => [
      name,
      new algo.LoadBalancer(
        targets, {
          algorithm,
          weight: t => t.weight,
          capacity: t => t.capacity,
        }
      )
    ]
  )
)

var $ctx
var $conn

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .pipe(
    function() {
      var lb = balancers[$ctx.route]
      if (lb) {
        $conn = lb.allocate()
        if ($conn) return 'proxy'
      }
      return 'pass'
    }, {
      'proxy': ($=>$
        .muxHTTP(() => $conn).to($=>$
          .connect(() => $conn.target.address)
        )
        .onEnd(() => $conn.free())
      ),
      'pass': ($=>$.pipeNext())
    }
  )
)
