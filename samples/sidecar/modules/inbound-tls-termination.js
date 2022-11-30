((
  config = pipy.solve('config.js'),
  certChain = config?.Certificate?.CertChain,
  issuingCA = config?.Certificate?.IssuingCA,
) => pipy()

.pipeline()
.branch(
  () => Boolean(certChain), (
    $=>$.acceptTLS({
      certificate: {
        cert: new crypto.Certificate(certChain),
        key: new crypto.PrivateKey(config?.Certificate?.PrivateKey),
      },
      trusted: issuingCA ? [new crypto.Certificate(issuingCA)] : [],
    }).to(
      $=>$.chain()
    )

  ), (
    $=>$.chain()
  )
)

)()
