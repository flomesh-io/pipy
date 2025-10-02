export default function (repoRoot) {
  var codebases = {}
  var files = {}

  if (repoRoot) {
    repoRoot = os.path.resolve(repoRoot)
    function searchDir(dir) {
      var list = os.readDir(dir)
      if (list.includes('codebase.json')) {
        var path = os.path.join('/', dir.substring(repoRoot.length))
        if (path.endsWith('/')) path = path.substring(0, path.length - 1)
        codebases[path] = Codebase(path)
      } else {
        list.filter(name => name.endsWith('/')).forEach(
          name => searchDir(os.path.join(dir, name))
        )
      }
    }
    searchDir(repoRoot)
  }

  function Codebase(path, base) {
    var version = null
    var committed = {}
    var edited = {}
    var deleted = new Set

    if (repoRoot) {
      var rootDir = os.path.join(repoRoot, path)
      var fileDir = os.path.join(rootDir, 'files')
      var metaFilename = os.path.join(rootDir, 'codebase.json')
      try {
        var metainfo = JSON.decode(os.read(metaFilename))
        version = 'version' in metainfo ? metainfo.version : null
        base = metainfo.base || null
        function searchDir(dir) {
          os.readDir(dir).forEach(name => {
            if (name.endsWith('/')) {
              searchDir(os.path.join(dir, name))
            } else {
              var filename = os.path.join('/', dir.substring(fileDir.length), name)
              committed[filename] = os.readFile(os.path.join(dir, name))
            }
          })
        }
        searchDir(fileDir)
      } catch {}
    }

    base = base || null

    if (fileDir && metaFilename) {
      os.mkdir(fileDir, { recursive: true })
      os.write(metaFilename, JSON.encode({ version, base }))
    }

    function forEachAncestor(cb) {
      var ancestors = new Set
      for (var p = base; p in codebases && !ancestors.has(p); p = codebases[p].getBase()) {
        ancestors.add(p)
        cb(codebases[p])
      }
    }

    function getInfo() {
      var erasedFiles = []
      var baseFiles = {}
      deleted.forEach(v => erasedFiles.push(v))
      forEachAncestor(p => p.getFiles().forEach(k => { baseFiles[k] = true }))
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
      if (base in codebases) return codebases[base].getFile(path)
      return null
    }

    function setFile(path, data) {
      deleted.delete(path)
      edited[path] = data
    }

    function deleteFile(path) {
      delete edited[path]
      deleted.add(path)
    }

    function commit(ver) {
      if (fileDir && metaFilename) {
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
        os.write(metaFilename, JSON.encode({ version: ver, base }))
      }
      Object.entries(edited).forEach(
        ([k, v]) => {
          committed[k] = v
          delete edited[k]
        }
      )
      deleted.forEach(
        k => {
          delete committed[k]
        }
      )
      deleted.clear()
      version = ver
    }

    function rollback() {
      edited = {}
      deleted.clear()
    }

    function erase() {
      if (rootDir) {
        try {
          os.rmdir(rootDir, { recursive: true })
        } catch {}
      }
    }

    return {
      getInfo,
      getPath: () => path,
      getVersion: () => version,
      getBase: () => base,
      getFiles: () => Object.keys(committed),
      getFile,
      setFile,
      deleteFile,
      commit,
      rollback,
      erase,
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
