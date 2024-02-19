pipy.read('input', $=>$
  .decodeDubbo()
  .replaceMessageBody(
    data => (
      Hessian.encode(
        Hessian.decode(data)
      )
    )
  )
  .encodeDubbo()
  .tee('-')
)
