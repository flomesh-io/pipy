import config from '/config.js'

var greetings = Object.fromEntries(
  Object.entries(config.hello).map(
    ([name, { head, body }]) => [
      name,
      new Message(head, body)
    ]
  )
)

var $ctx
var $greeting

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .pipe(
    function (evt) {
      if (evt instanceof MessageStart) {
        $greeting = greetings[$ctx.route]
        return $greeting ? 'greet' : 'pass'
      }
    }, {
      'greet': ($=>$
        .replaceData()
        .replaceMessage(() => $greeting)
      ),
      'pass': ($=>$.pipeNext())
    }
  )
)
