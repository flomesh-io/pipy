export default function (url, rootDir, argv, tls) {
  var agent = new http.Agent(
    url.host,
    {
      tls: url.protocol === 'https:' ? (tls || {}) : null
    }
  )

  var codebaseVersion = null
  var codebaseTime = ''
  var codebaseFiles = {}
  var subprocPID = 0

  function start() {
    download().then(obj => {
      if (!obj) {
        console.error('Failed to download codebase')
        return new Timeout(10).wait().then(start)
      }

      if (!('/main.js' in obj.files)) {
        console.error('File /main.js not found in codebase')
        return new Timeout(10).wait().then(start)
      }

      kill().then(() => {
        console.info('Starting subprocess')

        try {
          os.rmdir(rootDir, { recursive: true })
          os.mkdir(rootDir, { recursive: true })

          Object.entries(obj.files).forEach(
            ([path, file]) => {
              var dir = os.path.dirname(path)
              os.mkdir(os.path.join(rootDir, dir), { recursive: true })
              os.write(os.path.join(rootDir, path), file.data)
            }
          )

        } catch (err) {
          console.error(err)
          return new Timeout(10).wait().then(start)
        }

        codebaseVersion = obj.version
        codebaseTime = obj.time
        codebaseFiles = obj.files

        run()
        watch()
      })
    })
  }

  function download() {
    console.info('GET', url.href)

    return agent.request('GET', url.path).then(
      res => {
        var status = res?.head?.status
        if (status < 200 || status >= 300 || !status) {
          console.error('GET', url.href, 'response error', status)
          return null
        }

        var body = res?.body || new Data
        if (body.size > 1024 * 1024) {
          console.error('GET', url.href, 'response too large')
          return null
        }

        var version = res.head.headers['etag']
        var time = res.head.headers['last-modified']
        var files = {}
        var queue = body.toString().split('\n').filter(s=>s)

        function downloadNext() {
          if (queue.length === 0) {
            return { version, time, files }
          }

          var filePath = queue.shift()
          var httpPath = os.path.join(url.path, filePath)

          console.info('GET', httpPath)

          return agent.request('GET', httpPath).then(
            res => {
              var status = res?.head?.status
              if (status < 200 || status >= 300 || !status) {
                console.error('GET', httpPath, 'response error', status)
                return null
              }
              files[filePath] = {
                etag: res.head.headers['etag'] || '',
                data: res?.body || new Data
              }
              return downloadNext()
            }
          ).catch(() => null)
        }

        return downloadNext()
      }
    ).catch(() => null)
  }

  function run() {
    var cmdline = [
      pipy.argv[0],
      os.path.join(rootDir, 'main.js'),
      ...(argv || [])
    ]

    pipeline($=>$
      .onStart(new Data)
      .exec(
        cmdline, {
          stderr: true,
          onStart: pid => {
            subprocPID = pid
            console.info('Subprocess started with PID =', pid)
          },
          onExit: code => {
            console.info('Subprocess exited with code =', code)
            subprocPID = 0
          },
        }
      )
      .tee('-')
    ).spawn()
  }

  function kill() {
    if (subprocPID > 0) {
      function wait(t) {
        if (t > 0) {
          return new Timeout(1).wait().then(
            () => subprocPID === 0 || wait(t - 1)
          )
        }
      }

      os.kill(subprocPID)
      console.info('Sent SIGTERM to subprocess')

      return wait(10).then(() => {
        if (subprocPID > 0) {
          os.kill(subprocPID)
          console.info('Sent SIGTERM again to subprocess')
          return wait(5).then(() => {
            if (subprocPID > 0) {
              os.kill(subprocPID, 9)
              console.info('Sent SIGKILL to subprocess')
            }
          })
        }
      })
    } else {
      return Promise.resolve()
    }
  }

  function watch() {
    new Timeout(5).wait().then(
      () => agent.request('HEAD', url.path).catch(() => null)
    ).then(
      res => {
        var status = res?.head?.status
        if (status < 200 || status >= 300 || !status) {
          console.error('GET', url.href, 'response error', status)
          return watch()
        }
        var headers = res.head.headers
        if (headers['etag'] !== codebaseVersion || headers['last-modified'] !== codebaseTime) {
          console.info(
            'Found new update of codebase with version change',
            codebaseVersion, '=>', headers['etag'], 'and time change',
            codebaseTime, '=>', headers['last-modified']
          )
          return start()
        }
        return agent.request('GET', os.path.join(url.path, '_etags')).then(
          res => {
            var status = res?.head?.status
            if (status < 200 || status >= 300 || !status) return watch()
            if (res.body?.size > 1024 * 1024) return watch()
            var oldSet = new Set(Object.keys(codebaseFiles))
            var newSet = {}
            res.body.toString().split('\n').forEach(
              line => {
                line = line.trim()
                var i = line.lastIndexOf('#')
                if (i >= 0) {
                  var path = line.substring(0,i)
                  var etag = line.substring(i+1)
                  var old = codebaseFiles[path]
                  if (old?.etag !== etag) {
                    newSet[path] = true
                  }
                  oldSet.delete(path)
                }
              }
            )
            oldSet.forEach(path => { newSet[path] = true })
            return Promise.all(Object.keys(newSet).map(
              path => agent.request('GET', os.path.join(url.path, path)).then(
                res => {
                  var status = res?.head?.status
                  if (200 <= status && status < 300) {
                    var etag = res.head.headers['etag'] || ''
                    var data = res.body || new Data
                    codebaseFiles[path] = { etag, data }
                    var dir = os.path.dirname(path)
                    os.mkdir(os.path.join(rootDir, dir), { recursive: true })
                    os.write(os.path.join(rootDir, path), res.body || new Data)
                    console.info('Downloaded file', path)
                  } else if (status === 404) {
                    delete codebaseFiles[path]
                    os.rm(os.path.join(rootDir, path), { force: true })
                    console.info('Erased 404 file', path)
                  }
                }
              )
            )).then(() => watch())
          }
        )
      }
    ).catch(err => {
      console.error(err)
      watch()
    })
  }

  return { start, kill }
}
