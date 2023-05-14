#!/bin/bash

echo 'sending 10 separate requests..'

curl http://localhost:8000/
curl http://localhost:8000/ --http2
curl http://localhost:8000/
curl http://localhost:8000/ --http2-prior-knowledge
curl http://localhost:8000/
curl http://localhost:8000/ --http2
curl http://localhost:8000/
curl http://localhost:8000/ --http2-prior-knowledge
curl http://localhost:8000/
curl http://localhost:8000/ --http2

echo 'sending 10 requests in a batch...'

curl http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/

echo 'sending 10 requests in a batch with --http2...'

curl --http2 \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/

echo 'sending 10 requests in a batch with --http2-prior-knowledge...'

curl --http2-prior-knowledge \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/ \
     http://localhost:8000/

echo 'sending 10 separate requests over HTTPS...'

curl --insecure https://localhost:8443/
curl --insecure https://localhost:8443/ --http2
curl --insecure https://localhost:8443/
curl --insecure https://localhost:8443/ --http2
curl --insecure https://localhost:8443/
curl --insecure https://localhost:8443/ --http2
curl --insecure https://localhost:8443/
curl --insecure https://localhost:8443/ --http2
curl --insecure https://localhost:8443/
curl --insecure https://localhost:8443/ --http2

echo 'sending 10 requests in a batch over HTTPS...'

curl --insecure \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/

echo 'sending 10 requests in a batch over HTTPS with --http2...'

curl --insecure --http2 \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/ \
     https://localhost:8443/
