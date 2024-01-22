#!/bin/bash

echo 'Response compressed with gzip'
curl --compressed http://localhost:8080

echo 'Response compressed with deflate'
curl --compressed http://localhost:8081

echo 'Responses decompressed by proxy'
curl http://localhost:8000
curl http://localhost:8001
