pipy.read('input', $=>$
  .decodeBGP({ enableAS4: true })
  .encodeBGP({ enableAS4: true })
  .tee('-')
)
