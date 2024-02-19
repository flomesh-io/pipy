pipy.read('input', $=>$
  .decodeThrift()
  .encodeThrift()
  .tee('-')
)
