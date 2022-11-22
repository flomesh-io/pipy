((
) => pipy()

.export('main', {
  __flow: '',
})

.listen(15003, { transparent: true })
.onStart(() => (__flow = 'inbound', new Data))
.use('modules/inbound-main.js')

.listen(15001, { transparent: true })
.onStart(() => (__flow = 'outbound', new Data))
.use('modules/outbound-main.js')

)()
