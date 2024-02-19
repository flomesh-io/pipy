pipy.read('input', $=>$
  .decodeMQTT()
  .encodeMQTT()
  .tee('-')
)
