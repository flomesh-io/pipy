pipy.read('../../../deps/brotli-1.0.10/tests/testdata/ukkonooa.compressed', $=>$
  .decompress('brotli')
  .tee('-')
)
