import * as db from './db.js'

pipy.listen(8080, $=>$.serveHTTP(
  function (req) {
    var path = req.head.path
    if (path.startsWith('/api/')) {
      return api.find(path)(req)
    } else {
      return www.serve(req) || page404
    }
  }
))

var page404 = new Message({ status: 404 })

var www = new http.Directory('www/public')

var response = (status, obj) => new Message(
  {
    status,
    headers: { 'content-type': 'application/json' }
  },
  status === 204 ? undefined : JSON.encode(obj || { status })
)

var api = new algo.URLRouter({
  '/api/todos': function (req) {
    switch (req.head.method) {
      case 'GET': return response(200, db.listTodos())
      case 'POST': return response(201, db.insertTodo(req.body.toString()))
      default: return response(405)
    }
  },
  '/api/todos/*': function (req) {
    var id = req.head.path.substring(11) | 0
    switch (req.head.method) {
      case 'GET': return response(200, db.getTodo(id))
      case 'PATCH': return response(200, db.updateTodo(id, req.body.toString()))
      case 'DELETE': return response(204, db.deleteTodo(id))
      default: return response(405)
    }
  },
  '/api/check-todo/*': function (req) {
    return response(200, db.checkTodo(
      req.head.path.substring(16) | 0,
      req.body.toString() === 'true',
    ))
  },
  '/*': () => response(404)
})
