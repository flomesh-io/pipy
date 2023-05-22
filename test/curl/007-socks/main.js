pipy({
  _target: undefined,
})

.listen(8080)
.serveHTTP(
  new Message('Hello!\n')
)

.listen(8000)
.acceptSOCKS(
  ({ ip, domain, port }) => (
    _target = `${domain||ip}:${port}`,
    true
  )
).to(
  $=>$.connect(() => _target)
)

.listen(9000)
.connectSOCKS('localhost:8080').to($=>$
  .connect('localhost:8000')
)
