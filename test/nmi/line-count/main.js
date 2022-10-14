pipy()
  .import({
    __lineCount: 'line-count'
  })
  .read('./line-count.c')
  .use('../../../bin/line-count.so')
  .handleStreamEnd(() => (
    console.log('Line count:', __lineCount),
    pipy.exit()
  ))
