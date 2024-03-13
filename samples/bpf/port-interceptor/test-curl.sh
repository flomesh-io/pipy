#!/bin/bash

trap on_exit SIGINT

echo 'Creating a remote client...'
ip netns add test
ip link add test type veth peer name test netns test
ip link set test up
ip addr add 10.0.0.1/24 dev test
ip -n test link set test up
ip -n test addr add 10.0.0.2/24 dev test

echo 'Waiting a few seconds...'
sleep 3

echo 'Curl from the remote client...'
ip netns exec test curl http://10.0.0.1:8000 -i

echo 'Tear down the remote client...'
./test-clean.sh

echo 'Done.'
