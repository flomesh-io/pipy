var $params

export function createServer(routes, html) {
  routes = Object.entries(routes).map(
    ([k, v]) => ({
      match: k === '*' ? () => ({}) : new http.Match(k),
      pipelines: v,
    })
  )

  var serveHTML = pipeline($=>$
    .replaceData()
    .replaceMessage(
      req => html.serve(req) || new Message({ status: 404 })
    )
  )

  return pipeline($=>$
    .demuxHTTP().to($=>$
      .pipe(
        function (evt) {
          if (evt instanceof MessageStart) {
            var path = evt.head.path
            var route = routes.find(r => $params = r.match(path))
            if (!route) return html ? serveHTML : response404
            return route.pipelines[evt.head.method] || response405
          }
        },
        () => $params
      )
    )
  )
}

export function responder(f) {
  return pipeline($=>$
    .replaceMessage(req => {
      try {
        var res = f($params, req)
        if (res instanceof Promise) {
          return res.catch(responseError)
        } else {
          return res
        }
      } catch (e) {
        return responseError(e)
      }
    })
  )
}

export function response(status, body) {
  if (!body) return new Message({ status })
  if (typeof body === 'string') return responseCT(status, 'text/plain', body)
  if (body instanceof Data) return responseCT(status, 'application/octet-stream', body)
  return responseCT(status, 'application/json', JSON.encode(body))
}

function responseCT(status, ct, body) {
  return new Message(
    {
      status,
      headers: { 'content-type': ct }
    },
    body
  )
}

function responseError(e) {
  console.error(e)
  if (e instanceof Array && typeof e[0] === 'number') {
    return response(e[0], e[1])
  } if (typeof e === 'object') {
    return response(e.status || 500, e)
  } else {
    return response(500, { status: 500, message: e })
  }
}

var response404 = pipeline($=>$.replaceMessage(new Message({ status: 404 })))
var response405 = pipeline($=>$.replaceMessage(new Message({ status: 405 })))
