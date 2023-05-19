pipy({
  _target: undefined,
})

.listen(8080)
.serveHTTP(new Message('Hello!\n'))

.listen(8000)
.demuxHTTP().to($=>$
  .acceptHTTPTunnel(
    msg => msg.head.method === 'CONNECT' ? (
      void (_target = msg.head.path)
    ) : (
      new Message({ status: 405 }, 'method not allowed\n')
    )
  ).to($=>$
    .connect(() => _target)
  )
)

.listen(9000)
.connectHTTPTunnel(
  () => new Message({
    method: 'CONNECT',
    path: 'localhost:8080',
  })
).to($=>$
  .muxHTTP().to($=>$
    .connect('localhost:8000')
  )
)
