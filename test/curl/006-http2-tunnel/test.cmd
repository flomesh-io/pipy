@echo off

echo Send HTTP requests via HTTP/2 tunnel
curl http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000

echo Send HTTP/2 requests via HTTP/2 tunnel
curl --http2 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000
