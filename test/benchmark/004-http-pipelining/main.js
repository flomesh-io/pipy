pipy()

.listen(os.env.LISTEN || 8000)
.demuxHTTP().to($=>$
  .muxHTTP(() => 1).to($=>$
    .connect('localhost:8080')
  )
)
