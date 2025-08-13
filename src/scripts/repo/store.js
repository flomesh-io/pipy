export default function (repoRoot) {
  if (repoRoot) {
    repoRoot = os.path.resolve(repoRoot)
  }

  var codebases = {}
  var files = {}

  function Codebase(path, base) {
    var version = null
    var committed = {}
    var edited = {}
    var deleted = new Set

    if (repoRoot) {
      var rootDir = os.path.resolve(repoRoot, path)
      var fileDir = os.path.resolve(rootDir, 'files')
      var metaFilename = os.path.join(rootDir, 'codebase.json')
      try {
        var metainfo = JSON.decode(os.read(metaFilename))
        version = 'version' in metainfo ? metainfo.version : null
        base = metainfo.base || null
      } catch {}
      os.write(metaFilename, JSON.encode({ version, base }))
    } else {
      base = base || null
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
      if (repoRoot) {
        os.write(metaFilename, JSON.encode({ version: ver, base }))
      }
      Object.entries(edited).forEach(
        ([k, v]) => {
          var pathname = os.path.join(fileDir, k)
          os.mkdir(os.path.dirname(pathname), { recursive: true })
          os.write(pathname, v)
          committed[k] = v
          delete edited[k]
        }
      )
      deleted.forEach(
        k => {
          var pathname = os.path.join(fileDir, k)
          os.unlink(pathname)
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
      if (repoRoot) {
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
    if (path in codebases) {
      throw 'Codebase already exists'
    }
    if (Object.keys(codebases).some(p => path.startsWith(p))) {
      throw 'Nesting codebase path'
    }
    if (base && !(base in codebases)) {
      throw 'Base codebase not found'
    }
    return (codebases[path] = Codebase(path, base))
  }

  function getCodebase(path) {
    return codebases[path] || null
  }

  function deleteCodebase(path) {
    var codebase = codebases[path]
    if (!codebase) {
      throw 'Codebase not found'
    }
    if (codebase.getDerived().length > 0) {
      throw 'Derived codebases exist'
    }
    codebase.erase()
    delete codebases[path]
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
    getFile,
  }
}
