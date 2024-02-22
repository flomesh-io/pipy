var config = YAML.decode(pipy.load('config.yml'))

pipy.listen(config.listen, $=>$
  .demuxHTTP().to($=>$
    .replaceMessage(
      function (msg) {
        var head = msg.head
        var url = new URL(head.path)
        var path = url.pathname
        if (path === '/') path = '/index.php'
        return new Message(
          {
            keepAlive: true,
            params: {
              REQUEST_METHOD: head.method,
              REQUEST_URI: url.pathname,
              QUERY_STRING: url.query,
              SCRIPT_FILENAME: `${config.documents}${path}`,
              SCRIPT_NAME: path,
              ...Object.fromEntries(Object.entries(head.headers).map(
                ([k, v]) => ['HTTP_' + k.toUpperCase().replaceAll('-', '_'), v]
              )),
            },
          },
          msg.body
        )
      }
    )
    .muxFastCGI().to($=>$
      .connect(config.application)
    )
    .replaceMessageStart(() => new Data('HTTP/1.1 200 OK\r\n'))
    .replaceMessageEnd(new StreamEnd)
    .decodeHTTPResponse()
  )
)
