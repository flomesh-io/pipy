@echo off

echo Access a normal HTTP port
curl http://localhost:8000/ -i --no-progress-meter 2>&1

echo Access a port with concurrent connections limited to 1
curl http://localhost:8001/ -i --no-progress-meter 2>&1

echo Access that port a second time
curl http://localhost:8001/ -i --no-progress-meter 2>&1

echo Access a port that closes at all connections
curl http://localhost:8002/ -i --no-progress-meter 2>&1

echo Access a non-listening port with retries
curl http://localhost:8003/ -i --no-progress-meter 2>&1

exit 0
