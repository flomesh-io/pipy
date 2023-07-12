#!/bin/bash

echo 'Send 5 requests slowly'
curl http://localhost:8080
sleep 1
curl http://localhost:8080
sleep 1
curl http://localhost:8080
sleep 1
curl http://localhost:8080
sleep 1
curl http://localhost:8080

echo 'Sleeping for 5 seconds...'
sleep 5

echo 'Send 5 requests in one shot'
curl http://localhost:8080 \
     http://localhost:8080 \
     http://localhost:8080 \
     http://localhost:8080 \
     http://localhost:8080
