#!/bin/bash

curl http://localhost:8000/foo
curl http://localhost:8000/foo
curl http://localhost:8000/foo
curl http://localhost:8000/foo
curl http://localhost:8000/foo
curl http://localhost:8000/bar
curl http://localhost:8000/bar
curl http://localhost:8000/bar
curl http://localhost:8000/bar
curl http://localhost:8000/bar

curl http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/abc \
     http://localhost:8000/xyz \
     http://localhost:8000/bar \
     http://localhost:8000/bar \
     http://localhost:8000/bar \
     http://localhost:8000/bar \
     http://localhost:8000/bar

curl http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/bar \
     http://localhost:8000/bar \
     http://localhost:8000/bar

echo 'sleeping...'
sleep 6

curl http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/foo \
     http://localhost:8000/bar \
     http://localhost:8000/bar \
     http://localhost:8000/bar
