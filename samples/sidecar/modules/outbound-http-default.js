pipy()

.pipeline()
.replaceData()
.replaceMessage(
  new Message({ status: 404 }, 'Not found')
)
