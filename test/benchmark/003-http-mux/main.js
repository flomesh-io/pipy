var lb = new algo.LoadBalancer([ 'localhost:8080' ])

var $conn

pipy.listen(os.env.LISTEN || 8000, $=>$
  .demuxHTTP().to($=>$
    .muxHTTP(() => $conn = lb.allocate()).to($=>$
      .connect(() => $conn.target)
    )
    .onEnd(() => $conn.free())
  )
)
