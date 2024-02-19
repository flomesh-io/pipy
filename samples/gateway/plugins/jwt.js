import config from '/config.js'

var routes = config.jwt
var keys = new algo.Cache(
  kid => new crypto.PublicKey(
    pipy.load(`secret/${kid}.pem`)
  )
)

var INVALID_SIG = new Message({ status: 401 }, 'Invalid signature')
var INVALID_KEY = new Message({ status: 401 }, 'Invalid key')
var INVALID_TOKEN = new Message({ status: 401 }, 'Invalid token')

var $ctx
var $result

export default pipeline($=>$
  .onStart(ctx => void ($ctx = ctx))
  .pipe(
    function (evt) {
      if (evt instanceof MessageStart) {
        var r = routes[$ctx.route]
        if (!r) return 'pass'

        var header = evt.head.headers.authorization || ''
        var token = header.startsWith('Bearer ') ? header.substring(7) : header
        var jwt = new crypto.JWT(token)

        if (!jwt.isValid) {
          $result = INVALID_TOKEN
          return 'reject'
        }

        var kid = jwt.header?.kid
        if (!r.keys?.includes?.(kid)) {
          $result = INVALID_KEY
          return 'reject'
        }

        if (jwt.verify(keys.get(kid))) {
          return 'pass'
        } else {
          $result = INVALID_SIG
          return 'reject'
        }
      }

    }, {
      'reject': ($=>$
        .replaceData()
        .replaceMessage(() => $result)
      ),
      'pass': ($=>$.pipeNext())
    }
  )
)
