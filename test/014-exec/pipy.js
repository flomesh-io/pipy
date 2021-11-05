pipy({
  _g: {
    session: null,
  },
})

// Inbound
.listen(6080)
  .handleStreamStart(
    () => (
      _g.session || (
        _g.session = new Session(__filename, 'start'),
        _g.session.input(new Data)
      )
    )
  )
  .connect(
    '127.0.0.1:8080',
    {
      retryCount: -1,
      retryDelay: '1s',
    }
  )

// Start sub-process
.pipeline('start')
  .exec('../../bin/pipy mock.js')
  .print()
  .handleStreamEnd(
    () => (
      _g.session = null,
      console.log('Child process exited')
    )
  )