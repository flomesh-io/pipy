pipy.read('input', $=>$
  .decodeRESP()
  .encodeRESP()
  .tee('-')
)
