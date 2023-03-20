pipy()

  .task()
  .onStart(new Message)
  .read('input')
  .decodeMQTT()
  .encodeMQTT()
  .tee('-')
