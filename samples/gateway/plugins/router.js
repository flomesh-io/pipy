import config from '/config.js'

var router = new algo.URLRouter(
  Object.fromEntries(
    Object.entries(config.endpoints).map(
      function ([
        url,
        { route, rewrite }
      ]) {
        if (rewrite) {
          rewrite = [
            new RegExp(rewrite.pattern),
            rewrite.replace,
          ]
        }
        return [url, { route, rewrite }]
      }
    )
  )
)

var $ctx

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .handleMessageStart(
    function (msg) {
      var h = msg.head
      var r = router.find(
        h.headers.host,
        h.path,
      )
      var re = r?.rewrite
      if (re) {
        h.path = h.path.replace(re[0], re[1])
      }
      $ctx.route = r?.route
    }
  )
  .pipeNext()
)
