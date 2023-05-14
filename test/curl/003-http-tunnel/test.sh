#!/bin/bash

curl http://localhost:8000 \
     http://localhost:8000 \
     http://localhost:8000 \
     http://localhost:8000 \
     http://localhost:8000

curl --proxy http://localhost:8000 --proxytunnel \
     http://localhost:8080/ \
     http://localhost:8080/ \
     http://localhost:8080/ \
     http://localhost:8080/ \
     http://localhost:8080/
