#!/usr/bin/env -S pipy --args

import Codebase from './codebase.js'

var codebaseURL = ''
var codebaseDir = '~/.pipy/tmp'
var tlsSettings = null
var argvHost = [...pipy.argv]
var argvProc = []

try {
  function parseOptions(argv, f) {
    var pos = 0
    argv.forEach(opt => {
      if (opt.startsWith('-')) {
        var i = opt.indexOf('=')
        if (i > 0) {
          f(opt.substring(0, i), opt.substring(i + 1))
        } else {
          f(opt, true)
        }
      } else {
        f(pos++, opt)
      }
    })
  }

  var i = argvHost.indexOf('--args')
  if (i >= 0) {
    argvProc = argvHost.slice(i+1)
    argvHost = argvHost.slice(0,i)
  }

  parseOptions(
    argvHost.slice(1),
    function (opt, val) {
      if (opt === 0) {
        codebaseURL = val
      } else if (typeof opt === 'number') {
        throw `Redundant positional argument: ${val}`
      } else {
        switch (opt) {
        case '--codebase-dir':
          codebaseDir = val
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

  if (codebaseDir.startsWith('~/')) {
    codebaseDir = os.path.join(os.home(), codebaseDir.substring(2))
  }

  codebaseDir = os.path.resolve(codebaseDir)
  var dirname = os.path.dirname(codebaseDir)
  var basename = os.path.basename(codebaseDir)

  for (var i = 1; i < 100; i++) {
    try {
      if (!os.stat(codebaseDir)) {
        os.mkdir(codebaseDir, { recursive: true })
        break
      }
    } catch {}
    codebaseDir = os.path.join(dirname, basename + '.' + i)
  }

  if (i >= 100) {
    throw 'No empty directory found for temporary codebase'
  }

} catch (err) {
  println('pipy:', err.toString())
  if (typeof err === 'object') println(err)
  pipy.exit(-1)
}

println('Using temporary codebase directory', codebaseDir)

var codebase = Codebase(
  new URL(codebaseURL),
  codebaseDir,
  argvProc,
  tlsSettings,
)

pipy.exit(() => {
  codebase.kill()
  cleanup()
})

codebase.start()

function cleanup() {
  println('Cleaning up temporary codebase directory', codebaseDir)
  os.rmdir(codebaseDir, { recursive: true })
}
