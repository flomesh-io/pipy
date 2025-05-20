export function initStore(pathname) {
  var storeBasePath = pathname
  var codebases = {}

  function newCodebase(name, base) {
    return {}
  }

  Codebase.builtin().forEach(
    name => codebases[name] = newCodebase(name)
  )

  return {
    list: function () {
      return Object.keys(codebases).sort()
    },

    get: function (name) {
      return codebases[name] || null
    },

    delete: function (name) {
      var cb = codebases[name]
      if (cb) {
        cb.erase()
        delete codebases[name]
      }
    }
  }
}
