pipy()

.task()
.onStart(new Message)
.read('input')
.compress('gzip')
.decompress('gzip')
.tee('-')
