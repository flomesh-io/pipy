export default pipeline($=>$
  .replaceMessage(
    new Message(
      {
        status: 404,
        headers: {
          'content-type': 'text/plain',
        }
      },
      'No handler'
    )
  )
)
