pipy.read('input', $=>$
  .decodeHTTPResponse()
  .decodeMultipart()
  .encodeHTTPResponse()
  .tee('-')
)
