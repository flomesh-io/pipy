pipy()
  .task()
  .onStart(() => new Message)
  .use('../../../bin/counter-threads.so')
  .handleMessage(
    msg => console.log(msg.body.toString())
  )
