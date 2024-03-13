#!/usr/bin/env pipy

// The backend
pipy.listen(8000, $=>$
  .serveHTTP(
    new Message('Hi\n')
  )
)

// The proxy
pipy.listen(9000, $=>$
  .demuxHTTP().to($=>$
    .muxHTTP().to($=>$
      .connect('localhost:8000')
    )
    .handleMessageStart(
      function (msg) {
        msg.head.headers['server'] = 'Pipy interceptor'
      }
    )
  )
)
