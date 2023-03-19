pipy()

.task()
.onStart(new Message)
.read('input')
.decodeRESP()
.encodeRESP()
.tee('-')
