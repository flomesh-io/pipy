#!/usr/bin/env -S pipy --log-local=null --args

var rootpath = os.path.resolve('.')
var basepath = pipy.argv[1] || ''
var testcases = pipy.list(basepath)
  .filter(path => path === 'test.js' || path.endsWith('/test.js'))
  .map(path => os.path.join(basepath, os.path.dirname(path)))
  .sort()

var results = {}

function runOneTest() {
  os.chdir(rootpath)

  if (testcases.length === 0) {
    println('Summary:')
    Object.entries(results).forEach(
      ([k, v]) => {
        if (v) {
          println('  PASS ', k)
        } else {
          println('  FAIL ', k)
        }
      }
    )
    println('Done.')
    pipy.exit()
    return
  }

  var path = testcases.shift()
  println('Test', path)

  var kill

  startFGW(os.path.join(path)).then(f => {
    kill = f
    println('  Running test...')
    os.chdir(os.path.join(rootpath, path))
    var testMain = pipy.import(os.path.join('.', path, 'test.js')).default
    return testMain({ log })
  }).then(ok => {
    return new Timeout(1).wait().then(ok)
  }).then(ok => {
    kill()
    results[path] = ok
    println(ok ? '  Passed.' : '  Failed.')
    runOneTest()
  }).catch(err => {
    println(`Failed to run test ${path}:`, err)
  })
}

function startFGW(path) {
  var pipyFilename = os.path.resolve('../../bin', 'pipy')
  var scriptFilename = os.path.resolve('.', path, 'main.js')
  var logDirname = os.path.resolve('.logs', path)
  var logFilename = os.path.resolve(logDirname, 'out.log')

  var cmdline = [
    pipyFilename, scriptFilename,
    '--log-local-only',
    '--admin-port=6060',
  ]

  println(`  Starting pipy...`)

  os.mkdir(logDirname, { recursive: true })

  var startupCallback
  var killProcess
  var hasStarted = false

  pipeline($=>$
    .onStart(new Data)
    .insert(new Promise(r => killProcess = r))
    .exec(cmdline, { stderr: true })
    .tee(logFilename)
    .replaceStreamStart(evt => [new MessageStart, evt])
    .split('\n')
    .handleMessage(msg => {
      if (msg.body.toString().indexOf('Listening') >= 0) {
        if (!hasStarted) {
          println(`  Started.`)
          startupCallback(() => killProcess(new StreamEnd))
          hasStarted = true
        }
      }
    })
  ).spawn()

  return new Promise(r => startupCallback = r)
}

function log(a, b, c, d, e, f) {
  var n = 6
  if (f === undefined) n--
  if (e === undefined) n--
  if (d === undefined) n--
  if (c === undefined) n--
  if (b === undefined) n--
  if (a === undefined) n--
  if (n >= 1) print('   ', a)
  if (n >= 2) print('', b)
  if (n >= 3) print('', c)
  if (n >= 4) print('', d)
  if (n >= 5) print('', e)
  if (n >= 6) print('', f)
  println('')
}

if (testcases.length > 0) {
  runOneTest()
} else {
  println('No tests found')
}
