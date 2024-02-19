pipy.read('input', $=>$
  .compress('gzip')
  .decompress('gzip')
  .tee('-')
)
