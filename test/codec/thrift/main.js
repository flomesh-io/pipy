pipy()

  .task()
  .onStart(new Message)
  .read('input')
  .decodeThrift()
  .encodeThrift()
  .tee('-')
