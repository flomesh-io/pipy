pipy()

.listen(8000)
.demuxHTTP().to($=>$
  .branchMessageStart(
    msg => msg.head.path.startsWith('/api/'), (
      (
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
          '/api/todos': msg => (
            select(msg.head.method,
              'GET', () => (
                response(200)
              ),
              'POST', () => (
                response(201)
              ), (
                response(405)
              )
            )
          ),
          '/api/todos/*': msg => (
            (
              id = msg.head.path.substring(11)|0,
            ) => (
              select(msg.head.method,
                'GET', () => (
                  response(200)
                ),
                'PATCH', () => (
                  response(200)
                ),
                'DELETE', () => (
                  response(204)
                ), (
                  response(405)
                )
              )
            )
          )(),
          '/*': () => response(404),
        })
      ) => ($=>$
        .replaceMessage(
          msg => router.find(msg.head.path)(msg)
        )
      )
    )(), (
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
