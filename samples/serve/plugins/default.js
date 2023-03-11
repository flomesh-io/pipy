((
  BODY_404 = `
<html>
  <head>
    <title>Not Found</title>
  </head>
  <body>
    Not Found
  </body>
</html>
`
) => pipy()

  .pipeline()
  .replaceMessage(
    new Message(
      { status: 404 },
      BODY_404.trim(),
    )
  )

)()
