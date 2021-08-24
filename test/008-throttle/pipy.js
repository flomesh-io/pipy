// Throttle
//
// Usage: tap(limit[, account ])
//
// Use `tap` to throttle data rate (when `limit` is a string) or
// message rate (when `limit` is a number).
//
// When quota needs to be tallied on multiple accounts, provide
// an account name by the optional second argument `account`.
//
// When throttling message rate, `tap` can only be used after a raw
// data stream is decoded into messages.

pipy()

.listen(6080)
  .tap('100k') // Limit 100K bytes per second
  .decodeHTTPRequest()
  .tap(1000) // Limit to 1000 messages per second
  .replaceMessage(
    new Message('Hello!\n')
  )
  .encodeHTTPResponse()