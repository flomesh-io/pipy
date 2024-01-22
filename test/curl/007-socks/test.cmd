@echo off

echo Send HTTP requests over SOCKS4
curl --proxy socks4://127.0.0.1:8000 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080

echo Send HTTP requests over SOCKS4a
curl --proxy socks4a://127.0.0.1:8000 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080

echo Send HTTP requests over SOCKS5
curl --proxy socks5://127.0.0.1:8000 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080 ^
     http://localhost:8080

echo Proxy HTTP requests over SOCKS5
curl http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000 ^
     http://localhost:9000
