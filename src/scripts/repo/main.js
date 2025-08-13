#!/usr/bin/env -S pipy --args

import parseOptions from './options.js'
import initStore from './store.js'

import { createServer, responder, response } from './server.js'

var adminPort = 6060
var repoPathname = ''
var initPathname = ''

try {
  parseOptions(
    pipy.argv.slice(1),
    function (opt, val) {
      if (opt === 0) {
        repoPathname = val
      } else if (typeof opt === 'number') {
        throw `Redundant positional argument: ${val}`
      } else {
        switch (opt) {
        case '--init-repo':
          initPathname = val
          break
        case '--admin-port':
          adminPort = Number.parseInt(val)
          if (Number.isNaN(adminPort)) adminPort = val
          break
        default:
          throw `Unknown option in repo mode: ${opt}`
        }
      }
    }
  )

  var store = initStore(repoPathname, initPathname)

  pipy.listen(adminPort, createServer({
    '/repo': {
      'GET': responder(() => (
        response(200, store.listCommittedCodebases().join('\n') + '\n')
      ))
    },

    '/repo/{path}': {
      ...Object.fromEntries(
        ['GET', 'HEAD'].map(method => [
          method,
          responder(({ path }) => {
            var file = store.getFile('/' + path)
            if (file) {
              return new Message({
                headers: {
                  'etag': file.version,
                  'content-type': file.contentType,
                }
              }, file.content)
            } else {
              return response(404)
            }
          })
        ])
      )
    },

    '/api/v1/repo': {
      'GET': responder(() => (
        response(200, store.listCodebases().join('\n') + '\n')
      ))
    },

    '/api/v1/repo/{path}': {
      'GET': responder(({ path }) => {
        var codebase = store.getCodebase('/' + path)
        if (codebase) {
          return response(200, codebase.getInfo())
        } else {
          return response(404)
        }
      }),

      'POST': responder(({ path }, req) => {
        var info = JSON.decode(req.body)
        var codebase = store.newCodebase('/' + path, info.base)
        return response(201, codebase.getInfo())
      }),

      'PATCH': responder(({ path }, req) => {
        var info = JSON.decode(req.body)
        var codebase = store.getCodebase('/' + path)
        if (codebase) {
          if (info.version !== codebase.getVersion()) {
            codebase.commit(info.version)
          }
          return response(201, codebase.getInfo())
        } else {
          return response(404)
        }
      }),

      'DELETE': responder(({ path }) => {
        store.deleteCodebase('/' + path)
        return response(204)
      }),
    },

  }))

} catch (err) {
  println('pipy:', err.toString())
  println(err)
  pipy.exit(-1)
}
