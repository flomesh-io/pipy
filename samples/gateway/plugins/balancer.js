import config from '/config.js'

var policies = {
  'round-robin': algo.RoundRobinLoadBalancer,
  'least-work': algo.LeastWorkLoadBalancer,
}

var balancers = Object.fromEntries(
  Object.entries(config.upstreams).map(
    ([name, lb]) => [
      name,
      new policies[lb.policy](lb.targets)
    ]
  )
)

var $ctx
var $target

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .pipe(
    function() {
      var lb = balancers[$ctx.route]
      if (lb) {
        $target = lb.borrow()
        if ($target) return 'proxy'
      }
      return 'pass'
    }, {
      'proxy': ($=>$
        .muxHTTP(() => $target).to($=>$
          .connect(() => $target.id)
        )
      ),
      'pass': ($=>$.pipeNext())
    }
  )
)
