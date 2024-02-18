import config from '/config.js'

var sites = Object.fromEntries(
  Object.entries(config['serve-files']).map(
    ([name, { root, fs }]) => [
      name,
      new http.Directory(
        root, { fs }
      )
    ]
  )
)

var pageNotFound = new Message(
  {
    status: 404,
    headers: {
      'content-type': 'text/plain',
    }
  },
  'Page not found'
)

var $ctx
var $site

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .pipe(
    function (evt) {
      if (evt instanceof MessageStart) {
        $site = sites[$ctx.route]
        return $site ? 'serve' : 'pass'
      }
    }, {
      'serve': ($=>$
        .replaceData()
        .replaceMessage(req => $site.serve(req) || pageNotFound)
      ),
      'pass': ($=>$.pipeNext())
    }
  )
)
