pipy()

.task()
.onStart(new Message)
.read('input')
.decodeRESP()
.replaceMessage(
  msg => RESP.encode(msg.payload)
)
.tee('-')
