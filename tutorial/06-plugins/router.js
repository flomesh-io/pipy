pipy({
  _router: new algo.URLRouter({
    '/*': {
      service: 'service-1',
    },
    '/hi/*': {
      service: 'service-2',
      rewrite: [new RegExp('^/hi'), '/hello'],
    },
  }),

  _route: null,
})

.export('router', {
  __serviceID: '',
})

.pipeline('request')
  .handleMessageStart(
    msg => (
      _route = _router.find(
        msg.head.headers.host,
        msg.head.path,
      ),
      _route && (
        _route.rewrite && (
          msg.head.path = msg.head.path.replace(
            _route.rewrite[0],
            _route.rewrite[1],
          )
        ),
        __serviceID = _route.service
      )
    )
  )