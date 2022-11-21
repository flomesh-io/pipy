((
) => pipy()

.export('main', {
  __flow: '',
})

.listen(15003, { transparent: true })
.onStart(() => (__flow = 'inbound', new Data))
.use('modules/inbound-classification.js')

)()
