pipy()

.listen(os.env.LISTEN || 8000)
.decodeHTTPRequest()
.encodeHTTPRequest()
.connect('localhost:8080')
.decodeHTTPResponse()
.encodeHTTPResponse()
