#!/bin/bash

on_exit() {
  ./test-clean.sh
}

trap on_exit SIGINT

setup_net() {
  ip netns add $1
  ip link add $2 type veth peer name veth0 netns $1
  ip link set $2 up
  ip addr add $3 dev $2
  ip -n $1 link set veth0 up
  ip -n $1 addr add $4 dev veth0
}

echo 'Setting up network...'
setup_net client veth0 10.0.0.100/24 10.0.0.200/24
setup_net server1 veth1 10.0.1.100/24 10.0.1.200/24
setup_net server2 veth2 10.0.2.100/24 10.0.2.200/24
setup_net server3 veth3 10.0.3.100/24 10.0.3.200/24
sleep 1

echo 'Starting upstream servers...'
ip netns exec server1 ../../../bin/pipy -e 'pipy().listen(8080).serveHTTP(new Message("hi from server1\n"))' 2> /dev/null &
ip netns exec server2 ../../../bin/pipy -e 'pipy().listen(8080).serveHTTP(new Message("hi from server2\n"))' 2> /dev/null &
ip netns exec server3 ../../../bin/pipy -e 'pipy().listen(8080).serveHTTP(new Message("hi from server3\n"))' 2> /dev/null &
sleep 1

echo 'Starting proxy...'
../../../bin/pipy main.js
