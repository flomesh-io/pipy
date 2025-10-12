#!/usr/bin/env -S pipy --args

import initStore from './store.js'

import { createServer, responder, response } from './server.js'

var repoPathname = ''
var listenPort = 6060
var tlsSettings = null

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
        case '--listen':
          listenPort = val
          break
        case '--tls-cert':
          tlsSettings ??= {}
          tlsSettings.certificate ??= {}
          tlsSettings.certificate.cert = new crypto.Certificate(os.read(val))
          break
        case '--tls-key':
          tlsSettings ??= {}
          tlsSettings.certificate ??= {}
          tlsSettings.certificate.key = new crypto.PrivateKey(os.read(val))
          break
        case '--tls-trusted':
          tlsSettings ??= {}
          if (os.stat(val)?.isDirectory) {
            tlsSettings.trusted = os.readDir(val).map(
              name => new crypto.Certificate(os.read(os.path.join(val, name)))
            )
          } else {
            tlsSettings.trusted = [new crypto.Certificate(os.read(val))]
          }
          break
        default:
          throw `Unknown option in repo mode: ${opt}`
        }
      }
    }
  )

  var html = new http.Directory('/html')

  if (repoPathname.startsWith('http://') || repoPathname.startsWith('https://')) {
    var url = new URL(repoPathname)

    var www = pipeline($=>$
      .replaceData()
      .replaceMessage(req => html.serve(req) || response(404))
    )

    var proxy = repoPathname.startsWith('https://') ? (
      pipeline($=>$
        .muxHTTP().to($=>$
          .connectTLS(tlsSettings).to($=>$
            .connect(url.host)
          )
        )
      )
    ) : (
      pipeline($=>$
        .muxHTTP().to($=>$
          .connect(url.host)
        )
      )
    )

    pipy.listen(listenPort, $=>$
      .demuxHTTP().to($=>$
        .pipe(evt => {
          if (evt instanceof MessageStart) {
            var path = evt.head.path
            if (path.startsWith('/api/')) return proxy
            if (path.startsWith('/repo/') && isBrowserRequest(evt)) evt.head.path = '/repo/[...]/index.html'
            return www
          }
        })
      )
    )

    console.info('Started in repo proxy mode listening on port', listenPort, 'forwarding to', url.href)

  } else {
    var store = initStore(repoPathname)
    var service = createServer(
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
                if (isBrowserRequest(req)) {
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
            var body = req.body.toString()
            console.log('POST', req.head.path, 'BODY', body.toString())
            var path = '/' + params['*']
            var info = JSON.parse(body || '{}')
            var codebase = store.newCodebase(path, info.base)
            return response(201, codebase.getInfo())
          }),

          'PATCH': responder((params, req) => {
            var body = req.body.toString()
            console.log('PATCH', req.head.path, 'BODY', body.toString())
            var path = '/' + params['*']
            var info = JSON.parse(body || '{}')
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
            console.log('DELETE', req.head.path)
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

      // fallback
      pipeline($=>$
        .replaceData()
        .replaceMessage(
          req => html.serve(req) || response(404)
        )
      )
    )

    if (tlsSettings) {
      pipy.listen(listenPort, $=>$
        .acceptTLS(tlsSettings).to($=>$
          .pipe(service)
        )
      )
    } else {
      pipy.listen(listenPort, service)
    }

    console.info('Started in repo mode listening on port', listenPort)
  }

} catch (err) {
  println('pipy:', err.toString())
  if (typeof err === 'object') println(err)
  pipy.exit(-1)
}

function isBrowserRequest(req) {
  return (req.head.headers['accept']?.indexOf?.('text/html') >= 0)
}
