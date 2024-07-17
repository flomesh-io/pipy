#!/usr/bin/env -S pipy --args

var UPDATE_CHECK_INTERVAL = 5
var CHECK_POINT_INTERVAL = 30
var PIPY_OPTIONS = ['--no-graph']

var url = pipy.argv[1]
var rootDir = os.path.resolve(pipy.argv[2] || '')

if (!url) {
  println('missing a codebase URL')
  return pipy.exit(-1)
}

if (!url.startsWith('http://')) {
  println('invalid codebase URL', url)
  return pipy.exit(-1)
}

if (!dirExists(rootDir)) {
  println('local directory not found:', rootDir)
  return pipy.exit(-1)
}

url = new URL(url)

var client = new http.Agent(url.host)
var currentEtag = undefined
var currentDate = undefined
var currentPID = 0

function backup() {
  if (!dirExists(os.path.join(rootDir, 'script.bak'))) {
    if (dirExists(os.path.join(rootDir, 'script'))) {
      os.rename(
        os.path.join(rootDir, 'script'),
        os.path.join(rootDir, 'script.bak')
      )
    }
  }
}

function restore() {
  if (dirExists(os.path.join(rootDir, 'script.bak'))) {
    os.rmdir(os.path.join(rootDir, 'script'), { recursive: true, force: true })
    os.rename(
      os.path.join(rootDir, 'script.bak'),
      os.path.join(rootDir, 'script')
    )
  }
}

function checkPoint() {
  os.rmdir(os.path.join(rootDir, 'script.bak'), { recursive: true, force: true })
}

function fileExists(pathname) {
  var s = os.stat(pathname)
  return s && s.isFile()
}

function dirExists(pathname) {
  var s = os.stat(pathname)
  return s && s.isDirectory()
}

function update() {
  new Timeout(UPDATE_CHECK_INTERVAL).wait().then(() => {
    client.request('GET', url.path).then(res => {
      var status = res?.head?.status
      if (status !== 200) {
        console.error(`Download ${url.href} -> ${status}`)
        return update()
      }
      var headers = res.head.headers
      var etag = headers['etag'] || ''
      var date = headers['date'] || ''
      if (etag === currentEtag && date === currentDate) return update()
      console.info(`Codebase changed: etag = '${etag}', date = '${date}'`)
      backup()
      return Promise.all(
        res.body.toString().split('\n').map(
          path => client.request('GET', os.path.join(url.path, path)).then(
            res => {
              var status = res?.head?.status
              console.info(`Download ${path} -> ${res.head.status}`)
              return (status === 200 ? [path, res.body] : [path, null])
            }
          )
        )
      ).then(files => {
        var base = os.path.join(rootDir, 'script')
        var failures = false
        files.forEach(([path, data]) => {
          if (!data) return (failures = true)
          var fullpath = os.path.join(base, path)
          var dirname = os.path.dirname(fullpath)
          os.mkdir(dirname, { recursive: true })
          os.write(fullpath, data)
        })
        if (failures) {
          console.error(`Update failed due to download failure`)
          restore()
          update()
        } else {
          console.info(`Update downloaded`)
          currentEtag = etag
          currentDate = date
          start()
          new Timeout(CHECK_POINT_INTERVAL).wait().then(() => {
            checkPoint()
            update()
          })
        }
      })
    })
  })
}

function start() {
  if (currentPID === 0) {
    var pathPipy = pipy.argv[0]
    var pathMain = os.path.join(rootDir, 'script/main.js')
    if (fileExists(pathMain)) {
      var command = [pathPipy, ...PIPY_OPTIONS, pathMain]
      console.info('Starting', command)
      pipeline($=>$
        .onStart(new Data)
        .exec(command, {
          stderr: true,
          onStart: pid => {
            currentPID = pid
            console.info('Started PID =', pid)
          },
          onExit: () => {
            console.info('Process exited PID =', currentPID)
            currentPID = 0
          },
        })
        .tee('-')
      ).spawn()
    }
  } else {
    os.kill(currentPID, 1)
  }
}

restore()
start()
update()
