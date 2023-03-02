pipy()

.task()
.onStart(() => new Message)
.read('input')
.decodeDubbo()
.encodeDubbo()
.tee('-')
