((
  config = JSON.decode(
    pipy.load('config.json')
  ),

  main = (ctx) => (
    pipy(ctx)
  ),

  makeHeaderEnv = (headers) => (
    Object.fromEntries(
      Object.entries(headers).map(
        ([k, v]) => ['HTTP_' + k.toUpperCase().replaceAll('-', '_'), v]
      )
    )
  ),

  makeFastCGIRequest = (msg, url, path) => (
    url = new URL(msg.head.path),
    path = url.pathname,
    path === '/' && (path = '/index.php'),
    new Message(
      {
        keepAlive: true,
        params: {
          REQUEST_METHOD: msg.head.method,
          REQUEST_URI: url.pathname,
          QUERY_STRING: url.query,
          ...makeHeaderEnv(msg.head.headers),
          SCRIPT_FILENAME: `${config.documents}${path}`,
          SCRIPT_NAME: path,
        },
      },
      msg.body
    )
  ),

) => main()

.listen(config.listen)
.demuxHTTP().to($=>$
  .replaceMessage(makeFastCGIRequest)
  .muxFastCGI().to($=>$
    .connect(config.application)
  )
  .replaceMessageStart(() => new Data('HTTP/1.1 200 OK\r\n'))
  .replaceMessageEnd(new StreamEnd)
  .decodeHTTPResponse()
)

)()
