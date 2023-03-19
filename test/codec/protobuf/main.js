pipy()

.task()
.onStart(new Message)
.read('input')
.replaceStreamStart(evt => [new MessageStart, evt])
.replaceMessageBody(
  data => (
    protobuf.encode(
      protobuf.decode(data)
    )
  )
)
.tee('-')
