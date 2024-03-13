#!/bin/bash

echo 'Try accessing upstream servers...'
curl http://10.0.1.200:8080
curl http://10.0.2.200:8080
curl http://10.0.3.200:8080
sleep 1

echo 'Testing proxy...'
ip netns exec client curl http://10.0.0.100:8080
ip netns exec client curl http://10.0.0.100:8080
ip netns exec client curl http://10.0.0.100:8080
ip netns exec client curl http://10.0.0.100:8080
ip netns exec client curl http://10.0.0.100:8080

echo 'Done.'
