pipy()

.export('proxy', {
  __turnDown: false,
})

.listen(8000)
  .use(['router.js', 'balancer.js'], 'connect')
  .demuxHTTP('request')

.pipeline('request')
  .use(['router.js', 'balancer.js'], 'request', () => !__turnDown)
  .link(
    'bypass', () => __turnDown,
    'no-handler'
  )
  .use(['balancer.js', 'router.js'], 'response')

.pipeline('no-handler')
  .replaceMessage(
    new Message({ status: 404 }, 'No handler')
  )

.pipeline('bypass')
