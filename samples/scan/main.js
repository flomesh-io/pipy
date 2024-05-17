#!/usr/bin/env -S pipy --log-local=null --args

var options = parseOptions({
  defaults: {
    '--ports': '1-65535',
    '--concurrency': 100,
    '--timeout': 5,
  },
  shorthands: {
    '-p': '--ports',
    '-c': '--concurrency',
    '-t': '--timeout',
  },
})

function parseOptions({ defaults, shorthands }) {
  var args = []
  var opts = {}
  var lastOption

  pipy.argv.forEach(
    function (term, i) {
      if (i === 0) return
      if (lastOption) {
        if (term.startsWith('-')) throw `Value missing for option ${lastOption}`
        addOption(lastOption, term)
        lastOption = undefined
      } else if (term.startsWith('--')) {
        var kv = term.split('=')
        processOption(kv[0], kv[1])
      } else if (term.startsWith('-')) {
        if (term.length === 2) {
          processOption(term)
        } else {
          processOption(term.substring(0, 2), term.substring(2))
        }
      } else {
        args.push(term)
      }
    }
  )

  function processOption(name, value) {
    var k = shorthands[name] || name
    if (!(k in defaults)) throw `invalid option ${k}`
    if (typeof defaults[k] === 'boolean') {
      opts[k] = true
    } else if (value) {
      addOption(k, value)
    } else {
      lastOption = name
    }
  }

  function addOption(name, value) {
    var k = shorthands[name] || name
    switch (typeof defaults[k]) {
      case 'number': opts[k] = Number.parseFloat(value); break
      case 'string': opts[k] = value; break
      case 'object': (opts[k] ??= []).push(value); break
    }
  }

  return { args, ...defaults, ...opts }
}

if (options.args.length !== 1) {
  println('Usage: scan <options> <target host>')
  println('Options:')
  println('  -p, --ports       <start-end>  Port range, defaults to 1-65535')
  println('  -c, --concurrency <number>     Number of concurrent scanners, defaults to 100')
  println('  -t, --timeout     <seconds>    Connection timeout, defaults to 5')
  return
}

var target = options.args[0]
var ports = options['--ports'].split('-')
var startPort = ports[0] || 1
var endPort = ports[1] || 65535
var currentPort = startPort
var timeout = options['--timeout']

var $port
var $data

var results = []

pipeline($=>$
  .onStart(new Data)
  .forkJoin(Array(options['--concurrency'])).to($=>$
    .repeat(() => currentPort <= endPort).to($=>$
      .onStart(() => {
        $port = currentPort++
        $data = new Data
        println('Probe', $port)
      })
      .pipe(
        () => $port <= endPort ? 'probe' : 'end', {
          'probe': ($=>$
            .connect(() => `${target}:${$port}`, { connectTimeout: timeout, idleTimeout: 5 })
            .handleData(d => $data.push(d))
            .handleStreamEnd(evt => {
              if (evt.error === 'IdleTimeout') {
                results.push({
                  port: $port,
                  response: $data.toString(),
                })
              }
            })
          ),
          'end': ($=>$.replaceStreamStart(new StreamEnd))
        }
      )
    )
  )
  .replaceStreamStart(new StreamEnd)

).spawn().then(() => {
  println(`Scanned ${endPort - startPort + 1} ports from ${startPort} to ${endPort} at ${target}`)
  results.forEach(({ port, response }) => {
    println('  Port', port)
    if (response) {
      println('    Response:', response)
    }
  })
  println(`Found ${results.length} open ports`)
})
