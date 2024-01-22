@echo off

echo Access HTTP tunnel as a normal HTTP server
curl http://localhost:8000 ^
     http://localhost:8000 ^
     http://localhost:8000 ^
     http://localhost:8000 ^
     http://localhost:8000

echo Access HTTP tunnel using CONNECT
curl --proxy http://localhost:8000 --proxytunnel ^
     http://localhost:8080/ ^
     http://localhost:8080/ ^
     http://localhost:8080/ ^
     http://localhost:8080/ ^
     http://localhost:8080/

echo Access HTTP proxy that connects upstreams with HTTP tunnel
curl http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000
