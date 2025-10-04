#!/usr/bin/env -S pipy --args

import initStore from './store.js'

import { createServer, responder, response } from './server.js'

var repoPathname = ''
var adminPort = 6060

try {
  function parseOptions(argv, cb) {
    var pos = 0
    argv.forEach(opt => {
      if (opt.startsWith('-')) {
        var i = opt.indexOf('=')
        if (i > 0) {
          cb(opt.substring(0, i), opt.substring(i + 1))
        } else {
          cb(opt, true)
        }
      } else {
        cb(pos++, opt)
      }
    })
  }

  parseOptions(
    pipy.argv.slice(1),
    function (opt, val) {
      if (opt === 0) {
        repoPathname = val
      } else if (typeof opt === 'number') {
        throw `Redundant positional argument: ${val}`
      } else {
        switch (opt) {
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

  var html = new http.Directory('/html')
  var store = initStore(repoPathname)

  pipy.listen(adminPort, createServer(
    {
      '/repo': {
        'GET': responder(() => (
          response(200, store.listCommittedCodebases().join('\n') + '\n')
        ))
      },

      '/repo/*': {
        ...Object.fromEntries(
          ['GET', 'HEAD'].map(method => [
            method,
            responder((params, req) => {
              var path = '/' + params['*']
              var isBrowser = (req.head.headers['accept']?.indexOf?.('text/html') >= 0)
              if (isBrowser) {
                req.head.path = '/repo/[...]/index.html'
                return html.serve(req)
              }
              var file = store.getFile(path)
              if (file) {
                return new Message({
                  headers: {
                    'etag': file.version,
                    'last-modified': file.time,
                    'content-type': file.contentType,
                  }
                }, file.content)
              }
              return response(404)
            })
          ])
        )
      },

      '/api/v1/repo': {
        'GET': responder(() => (
          response(200, store.listCodebases().join('\n') + '\n')
        ))
      },

      '/api/v1/repo/*': {
        'GET': responder(params => {
          var path = '/' + params['*']
          var codebase = store.getCodebase(path)
          if (codebase) return response(200, codebase.getInfo())
          return response(404)
        }),

        'POST': responder((params, req) => {
          var path = '/' + params['*']
          var info = JSON.decode(req.body)
          var codebase = store.newCodebase(path, info.base)
          return response(201, codebase.getInfo())
        }),

        'PATCH': responder((params, req) => {
          var path = '/' + params['*']
          var info = JSON.decode(req.body)
          var codebase = store.getCodebase(path)
          if (codebase) {
            if (info.version !== codebase.getVersion()) {
              codebase.commit(info.version)
            }
            return response(201, codebase.getInfo())
          }
          return response(404)
        }),

        'DELETE': responder(params => {
          var path = '/' + params['*']
          store.deleteCodebase(path)
          return response(204)
        }),
      },

      '/api/v1/repo-files/*': {
        'GET': responder(params => {
          var path = '/' + params['*']
          var codebase = store.findCodebase(path)
          if (codebase) {
            var filePath = path.substring(codebase.getPath().length)
            var data = codebase.getFile(filePath)
            if (data) return new Message(data)
          }
          return response(404)
        }),

        'POST': responder((params, req) => {
          var path = '/' + params['*']
          var codebase = store.findCodebase(path)
          if (codebase) {
            var filePath = path.substring(codebase.getPath().length)
            codebase.setFile(filePath, req.body)
            return response(201)
          }
          return response(404)
        }),

        'DELETE': responder(params => {
          var path = '/' + params['*']
          var codebase = store.findCodebase(path)
          if (codebase) {
            var filePath = path.substring(codebase.getPath().length)
            codebase.deleteFile(filePath)
            return response(204)
          }
          return response(404)
        }),
      },

    },
    html
  ))

} catch (err) {
  println('pipy:', err.toString())
  if (typeof err === 'object') println(err)
  pipy.exit(-1)
}
