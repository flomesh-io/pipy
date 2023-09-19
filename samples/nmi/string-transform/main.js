pipy()

.import({
  __stringMap: 'string-transform',
})

.listen(8080)
.demuxHTTP().to($=>$
  .onStart((
    (
      mapping = {
        'Hello': '你好',
      }
    ) => (
      () => void (__stringMap = mapping)
    )
  )())
  .replaceData()
  .replaceMessage((
    (
      site = new http.Directory('www'),
      p404 = new Message({ status: 404 }),
    ) => (
      req => site.serve(req) || p404
    )
  )())
  .use('../../../bin/string-transform.so')
)
