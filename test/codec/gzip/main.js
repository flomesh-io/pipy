pipy()

.task()
.onStart(new Message)
.read('input')
.compress('gzip')
.decompress('inflate')
.tee('-')
