#!/usr/bin/env -S pipy --args

import { createServer, responder, response } from './server.js'
import { initStore } from './store.js'

var options = JSON.parse(pipy.argv[1])
var store = initStore(options.pathname || '')

pipy.listen(options.listen, createServer({
  '/repo': {
    'GET': responder(() => Promise.resolve(
      response(200, store.list().join('\n') + '\n')
    ))
  },
}))
