pipy()

.pipeline('request')
  .replaceMessage(
    new Message({ status: 404 }, 'No handler')
  )
