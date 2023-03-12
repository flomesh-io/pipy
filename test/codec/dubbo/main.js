pipy()

.task()
.onStart(new Message)
.read('input')
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
