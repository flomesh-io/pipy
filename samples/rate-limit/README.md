# Sample: Global rate limiting

To limit message rate on a single thread is simple: just add a *throttleMessageRate()* filter on the request path, giving it a properly set *algo.Quota* object based on whatever rules you need differently for each request, and you are done.

```js
pipy.listen(8000, $=>$
  .demuxHTTP().to($=>$
    .throttleMessageRate(new algo.Quota(1000, { per: 1 }))
    .muxHTTP().to($=>$
      .connect('localhost:8080')
    )
  )
)
```

To limit message rate globally over multiple threads or even a cluster of proxy nodes, some extra work needs to be done as to how the states of *algo.Quota* objects are synchronized among all proxy threads/processes.

Thanks to Pipy's flexible pipeline system, global rate limiting can be done as easily as in this example. Basically it only involes 2 major parts:

First, we run a simple throttled HTTP service (port 8001 in this example) that takes in empty requests and throttles responses according to their paths. The actual throttling is done by the same *throttleMessageRate()* filter as in the single-threaded case. Similarly, this service is run on only one single thread globally and invoked by multiple proxy instances.

```js
var $quota

pipy.listen(8001, $=>$
  .demuxHTTP().to($=>$
    .handleMessageStart(
      function (msg) {
        $quota = ... // Decide what quota
      }
    )
    .throttleMessageRate(() => $quota)
    .replaceMessage(new Message) // Respond OK
  )
)
```

Second, in each proxy instance, right before we forward a request to the backend service using *muxHTTP()*, we generate an empty request and send it to the globally shared single-threaded throttling service on port 8001 via a *forkJoin()* filter. The original request won't continue until the subpipeline forked out from *forkJoin()* ends up with a *StreamEnd*, meaning that a response is received from the throttling service.

```js
pipy.listen(8000, $=>$
  .demuxHTTP().to($=>$
    .forkJoin([1]).to($=>$ // [1], [2] or [whatever], as long as its length is one
      .onStart(new Message) // Generate an empty request
      .muxHTTP({ version: 2 }).to($=>$
        .connect('localhost:8001')
      )
      .replaceMessage(new StreamEnd) // forkJoin() needs this to know it's over
    )
    .muxHTTP().to($=>$
      .connect('localhost:8080')
    )
  )
)
```

Because the messages being throttled at first are really just empty requests (well, *almost* empty requests, with only paths for deciding the rate limitations), which in turn limit the rate of the original requests, the communication between all proxy nodes and the central rate limiting service is kept minimal. To speed up the communication even further, we also leverage HTTP/2 for this matter.
