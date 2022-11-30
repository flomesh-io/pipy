((
  config = pipy.solve('config.js'),

) => pipy()

.export('main', {
  __flow: '',
})

.branch(
  Boolean(config?.Inbound?.TrafficMatches), (
    $=>$
    .listen(15003, { transparent: true })
    .onStart(() => (__flow = 'inbound', new Data))
    .use('modules/inbound-main.js')
  )
)

.branch(
  Boolean(config?.Outbound || config?.Spec?.Traffic?.EnableEgress), (
    $=>$
    .listen(15001, { transparent: true })
    .onStart(() => (__flow = 'outbound', new Data))
    .use('modules/outbound-main.js')
  )
)

)()
