#!/bin/bash

echo 'Response compressed with gzip'
curl -i --compressed http://localhost:8080

echo 'Response compressed with deflate'
curl -i --compressed http://localhost:8081

echo 'Responses decompressed by proxy'
curl -i http://localhost:8000
curl -i http://localhost:8001
