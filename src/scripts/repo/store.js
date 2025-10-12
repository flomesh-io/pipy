export default function (repoRoot) {
  var codebases = {}
  var files = {}

  if (repoRoot) {
    repoRoot = os.path.resolve(repoRoot)

    function searchDir(dir) {
      var list = os.readDir(dir)
      if (list.includes('__codebase__.json')) {
        var path = os.path.join('/', dir.substring(repoRoot.length))
        if (path.endsWith('/')) path = path.substring(0, path.length - 1)
        var cb = Codebase(path)
        codebases[path] = cb
      } else {
        list.filter(name => name.endsWith('/')).forEach(
          name => searchDir(os.path.join(dir, name))
        )
      }
    }

    searchDir(repoRoot)

    Object.values(codebases).forEach(
      cb => {
        if (cb.getVersion()) {
          cb.generate()
        }
      }
    )
  }

  function Codebase(path, base) {
    var version = null
    var time = null
    var committed = {}
    var patched = {}
    var edited = {}
    var deleted = new Set

    if (repoRoot) {
      var fileDir = os.path.join(repoRoot, path)
      var metaFilename = os.path.join(fileDir, '__codebase__.json')
      try {
        var metainfo = JSON.decode(os.read(metaFilename))
        version = 'version' in metainfo ? metainfo.version : null
        time = metainfo.time || Date.now()
        base = metainfo.base || null
        patched = metainfo.patched || {}
      } catch {}
    }

    time = time || Date.now()
    base = base || null

    saveInfo()

    if (fileDir) {
      function searchDir(dir) {
        os.readDir(dir).forEach(name => {
          if (name === '__codebase__.json') return
          if (name.endsWith('/')) {
            searchDir(os.path.join(dir, name))
          } else {
            var filename = os.path.join('/', dir.substring(fileDir.length), name)
            committed[filename] = os.read(os.path.join(dir, name))
          }
        })
      }
      searchDir(fileDir)
    }

    function forEachAncestor(cb) {
      var ancestors = new Set
      for (var p = base; p in codebases && !ancestors.has(p); p = codebases[p].getBase()) {
        ancestors.add(p)
        var ret = cb(codebases[p])
        if (ret) return ret
      }
    }

    function saveInfo() {
      if (fileDir && metaFilename) {
        os.mkdir(fileDir, { recursive: true })
        os.write(metaFilename, JSON.encode({ version, time, base, patched }))
      }
    }

    function getInfo() {
      var erasedFiles = []
      var baseFiles = {}
      deleted.forEach(v => erasedFiles.push(v))
      forEachAncestor(p => p.allCommittedFiles().forEach(k => { baseFiles[k] = true }))
      return {
        path,
        base,
        version,
        files: Object.keys(committed),
        editFiles: Object.keys(edited),
        erasedFiles,
        baseFiles: Object.keys(baseFiles),
        derived: getDerived(),
      }
    }

    function getDerived() {
      return (
        Object.entries(codebases).filter(([_, v]) => v.getBase() === path).map(([k]) => k)
      )
    }

    function getFile(path) {
      if (deleted.has(path)) return null
      if (path in edited) return edited[path]
      if (path in committed) return committed[path]
      return forEachAncestor(p => p.getCommittedFile(path)) || null
    }

    function setFile(path, data) {
      if (!path.startsWith('/__codebase__.json')) {
        deleted.delete(path)
        edited[path] = data
      }
    }

    function deleteFile(path) {
      if (!path.startsWith('/__codebase__.json')) {
        delete edited[path]
        deleted.add(path)
      }
    }

    function commit(ver) {
      if (ver || !version) {
        if (!ver) {
          ver = '1'
          time = Date.now()
        }
        version = ver
        var isPatch = false
      } else {
        var isPatch = true
      }

      if (fileDir) {
        Object.entries(edited).forEach(
          ([k, v]) => {
            var pathname = os.path.join(fileDir, k)
            os.mkdir(os.path.dirname(pathname), { recursive: true })
            os.write(pathname, v)
          }
        )
        deleted.forEach(
          k => {
            var pathname = os.path.join(fileDir, k)
            os.unlink(pathname)
          }
        )
      }

      Object.entries(edited).forEach(
        ([k, v]) => {
          committed[k] = v
          if (isPatch) patched[k] = Date.now()
        }
      )

      deleted.forEach(k => {
        delete committed[k]
        if (isPatch) delete patched[k]
      })

      deleted.clear()
      edited = {}
      if (!isPatch) patched = {}

      saveInfo()

      if (isPatch) {
        generate()
      } else {
        var t = Date.now()
        var done = new Set
        for (var queue = [path]; queue.length > 0; done.add(k)) {
          var k = queue.pop()
          var cb = codebases[k]
          if (cb) {
            cb.generate(t)
            cb.getDerived().filter(k => !done.has(k)).forEach(
              k => queue.push(k)
            )
          }
        }
      }
    }

    function rollback() {
      edited = {}
      deleted.clear()
    }

    function erase() {
      if (fileDir) {
        try {
          os.rmdir(fileDir, { recursive: true })
        } catch {}
      }
    }

    function generate(t) {
      if (t) {
        time = t
        saveInfo()
      }

      var prefix = os.path.join(path, '/')
      Object.keys(files).filter(p => p.startsWith(prefix)).forEach(
        path => delete files[path]
      )

      var all = { ...committed }
      forEachAncestor(
        cb => cb.allCommittedFiles().forEach(
          path => {
            all[path] ??= cb.getCommittedFile(path)
          }
        )
      )

      var paths = Object.keys(all)
      var index = new Data
      var etags = new Data
      var ts = new Date(time).toUTCString()

      paths.forEach(
        p => {
          var fullpath = os.path.join(prefix, p)
          var v = version.toString()
          var t = ts
          var pt = patched[p]
          if (pt) {
            t = new Date(pt).toUTCString()
            v = v + '.' + pt
          }
          files[fullpath] = {
            version: v,
            time: t,
            contentType: 'text/plain',
            content: all[p],
          }
          index.push(p)
          index.push('\n')
          etags.push(p)
          etags.push('#')
          etags.push(v)
          etags.push('\n')
        }
      )

      files[prefix] = {
        version,
        time: ts,
        contentType: 'text/plain',
        content: index,
      }

      files[os.path.join(prefix, '_etags')] = {
        version,
        time: ts,
        contentType: 'text/plain',
        content: etags,
      }
    }

    return {
      getInfo,
      getPath: () => path,
      getVersion: () => version,
      getBase: () => base,
      getDerived,
      allCommittedFiles: () => Object.keys(committed),
      getCommittedFile: path => committed[path] || null,
      getFile,
      setFile,
      deleteFile,
      commit,
      rollback,
      erase,
      generate,
    }
  }

  function listCodebases() {
    return Object.keys(codebases).sort()
  }

  function listCommittedCodebases() {
    return Object.values(codebases).filter(c => c.getVersion() !== null).map(c => c.getPath()).sort()
  }

  function newCodebase(path, base) {
    var name = path.endsWith('/') ? path.substring(0, path.length - 1) : path
    if (name in codebases) {
      throw [400, 'Codebase already exists']
    }
    path = name + '/'
    if (Object.keys(codebases).some(p => path.startsWith(p + '/'))) {
      throw [400, 'Nesting codebase path']
    }
    if (base && !(base in codebases)) {
      throw [400, 'Base codebase not found']
    }
    return (codebases[name] = Codebase(name, base))
  }

  function getCodebase(path) {
    return codebases[path] || null
  }

  function deleteCodebase(path) {
    var codebase = codebases[path]
    if (!codebase) {
      throw [404, 'Codebase not found']
    }
    if (codebase.getDerived().length > 0) {
      throw [400, 'Derived codebases exist']
    }
    codebase.erase()
    delete codebases[path]
  }

  function findCodebase(path) {
    var name = Object.keys(codebases).find(p => path.startsWith(p + '/'))
    if (name) return codebases[name]
    return null
  }

  function getFile(path) {
    return files[path] || null
  }

  return {
    listCodebases,
    listCommittedCodebases,
    newCodebase,
    getCodebase,
    deleteCodebase,
    findCodebase,
    getFile,
  }
}
