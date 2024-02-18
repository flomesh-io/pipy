pipy()
  .pipeline()
  .replaceMessage(
    new Message({ status: 404 }, 'No handler')
  )
