pipy()

.listen(8000)
.demuxHTTP().to($=>$
  .branchMessageStart(

    // API endpoints
    msg => msg.head.path.startsWith('/api/'), (
      (
        db = pipy.solve('db.js'),

        response = (status, obj) => (
          new Message(
            {
              status,
              headers: {
                'content-type': 'application/json'
              }
            },
            JSON.encode(obj || { status })
          )
        ),

        router = new algo.URLRouter({
          '/api/todos': req => (
            select(req.head.method,
              'GET', () => (
                response(200, db.listTodos())
              ),
              'POST', () => (
                response(201, db.insertTodo(req.body.toString()))
              ), (
                response(405)
              )
            )
          ),

          '/api/todos/*': req => (
            (
              id = req.head.path.substring(11)|0,
            ) => (
              select(req.head.method,
                'GET', () => (
                  response(200, db.getTodo(id))
                ),
                'PATCH', () => (
                  response(200, db.updateTodo(id, req.body.toString()))
                ),
                'DELETE', () => (
                  response(204, db.deleteTodo(id))
                ), (
                  response(405)
                )
              )
            )
          )(),

          '/*': () => response(404),
        }),

      ) => ($=>$
        .replaceMessage(
          req => router.find(req.head.path)(req)
        )
      )
    )(),

    // Static files
    (
      (
        www = new http.Directory('www/public')
      ) => ($=>$
        .replaceMessage(
          req => www.serve(req)
        )
      )
    )()
  )
)
