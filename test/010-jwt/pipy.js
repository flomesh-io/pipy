pipy({
  _JWKS_ENDPOINTS: [
    'http://127.0.0.1:6081/api/v1/jwks',
    'https://127.0.0.1:6082/api/v1/jwks',
  ],

  _g: {
    keystores: [],
  },

  _result: '',

  _verify: jwt => (
    ((
      kid,
      key,
    ) => (
      jwt = new crypto.JWT(jwt || ''),
      jwt.isValid ? (
        key = null,
        kid = jwt.header?.kid,
        _g.keystores.find(keys => key = keys?.find?.(k => k.kid === kid)),
        key ? (
          jwt.verify(key.jwk) ? 'OK' : 'Invalid signature'
        ) : (
          'Invalid key'
        )
      ) : (
        'Invalid token'
      )
    ))()
  ),
})

.listen(6080)
  .fork('verify')
  .wait(
    () => _result !== ''
  )
  .link(
    'pass', () => _result === 'OK',
    'ban'
  )

.pipeline('pass')
  .connect('127.0.0.1:8080')

.pipeline('ban')
  .decodeHTTPRequest()
  .replaceMessage(
    () => new Message({ status: 401 }, _result)
  )
  .encodeHTTPResponse()

.pipeline('verify')
  .decodeHTTPRequest()
  .handleMessageStart(
    evt => (
      ((
        token,
      ) => (
        token = evt.head.headers.authorization || '',
        token.startsWith('Bearer ') && (token = token.substring(7)),
        _result = _verify(token)
      ))()
    )
  )

.task('30s')
  .handleStreamStart(
    () => (
      console.log('Updating keys...'),
      _g.keystores = _JWKS_ENDPOINTS.map(
        (_, i) => _g.keystores[i]
      )
    )
  )
  .fork(
    'get-keys',
    () => _JWKS_ENDPOINTS.map(
      (url, i) => ({ _i: i, _url: new URL(url) })
    )
  )
  .wait(
    () => _g.keystores.every(v => v)
  )
  .replaceMessage(
    () => (
      console.log('Keys updated'),
      new StreamEnd
    )
  )

.pipeline('get-keys')
  .handleMessageStart(
    () => console.log(`Getting keys from ${_url.href}...`)
  )
  .replaceMessage(
    () => new Message({
      method: 'GET',
      path: _url.path,
      headers: {
        host: _url.host,
      },
    })
  )
  .encodeHTTPRequest()
  .connect(
    () => _url.host
  )
  .decodeHTTPResponse()
  .handleMessage(
    msg => (
      _g.keystores[_i] = JSON.decode(msg.body)?.keys || [],
      _g.keystores[_i].forEach(k => k.jwk = new crypto.JWK(k)),
      console.log(`Got ${_g.keystores[_i].length} key(s) from ${_url.href}`)
    )
  )

// Mock backend service on port 8080
.listen(8080)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message('Hello!\n')
  )
  .encodeHTTPResponse()

// Mock key store service on port 6081
.listen(6081)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message(os.readFile('./keys-1.json'))
  )
  .encodeHTTPResponse()

// Mock key store service on port 6082
.listen(6082)
  .decodeHTTPRequest()
  .replaceMessage(
    new Message(os.readFile('./keys-2.json'))
  )
  .encodeHTTPResponse()