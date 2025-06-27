var $msgID

pipy.listen(8000, $=>$
  .split('\n')
  .demuxQueue().to($=>$
    .replaceMessage(getMessageID)
    .mux(() => Math.floor(Math.random() * 99), {
      maxSessions: 1,
      messageKey: () => $msgID,
    }).to($=>$
      .replaceMessage(
        msg => msg.body.push('\n')
      )
      .connect('localhost:8080')
      .split('\n')
      .replaceMessage(getMessageID)
    )
  )
  .replaceMessage(msg => msg.body.push('\n'))
)

function getMessageID(msg) {
  if (msg.body?.size > 0) {
    var data = JSON.decode(msg.body)
    $msgID = data.id
    return msg
  }
}
