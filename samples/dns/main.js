var config = YAML.decode(pipy.load('config.yaml'))

pipy.listen(config.listen, 'udp', $=>$
  .connect(config.server, { protocol: 'udp' })
  .replaceData(
    function (dgram) {
      var msg = DNS.decode(dgram)
      msg.answer = msg.answer.filter(
        rec => config.block.indexOf(rec.name) < 0
      )
      return DNS.encode(msg)
    }
  )
)
