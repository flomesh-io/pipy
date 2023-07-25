pipy()

.repeat(10, ($, i) => ($
  .listen(8001 + i)
  .serveHTTP(
    ((
      body = `Hello from ${8001 + i}: ` + 'x'.repeat(2000 * i) + '\n',
      counter = 0,
    ) => (
      msg => (counter++ % 100 > 0) ? (
        new Message(
          { headers: msg.head.headers }, body
        )
      ) : (
        new Timeout(1).wait().then(
          new Message(
            { headers: msg.head.headers }, body
          )
        )
      )
    ))()
  )
))

.listen(8123)
.throttleDataRate(new algo.Quota(1e5, { per: 1 }))
.serveHTTP(new Message('ok'))
