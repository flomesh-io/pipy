pipy()

.listen(os.env.LISTEN || 8000)
.demuxHTTP().to($=>$
  .muxHTTP().to($=>$
    .connect('localhost:8080')
  )
)
