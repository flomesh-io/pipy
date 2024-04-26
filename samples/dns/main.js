var config = YAML.decode(pipy.load('config.yaml'))

pipy.listen(config.listen, 'udp', $=>$
  .connect(config.server, { protocol: 'udp' })
  .replaceData(
    function (dgram) {
      var msg = DNS.decode(dgram)
      msg.answer = msg.answer.filter(
        rec => !config.block.includes(rec.name)
      )
      return DNS.encode(msg)
    }
  )
)
