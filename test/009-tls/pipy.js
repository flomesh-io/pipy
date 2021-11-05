pipy()

.listen(6080)
  .connectTLS(
    'tls-encrypted',
    {
      certificate: {
        cert: new crypto.Certificate(os.readFile('client-cert.pem')),
        key: new crypto.PrivateKey(os.readFile('client-key.pem')),
      },
      trusted: [
        new crypto.Certificate(os.readFile('ca-cert.pem')),
      ],
    }
  )
  .dump()

.listen(6060)
  .link('tls-offloaded')
  .dump()

.pipeline('tls-encrypted')
  .connect('127.0.0.1:6443')

.listen(6443)
  .acceptTLS(
    'tls-offloaded', {
      certificate: {
        cert: new crypto.Certificate(os.readFile('server-cert.pem')),
        key: new crypto.PrivateKey(os.readFile('server-key.pem')),
      },
      trusted: [
        new crypto.Certificate(os.readFile('client-cert.pem')),
      ],
    }
  )

.pipeline('tls-offloaded')
  .serveHTTP(
    new Message('Hi, there!\n')
  )