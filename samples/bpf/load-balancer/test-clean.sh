#!/bin/bash

ip link del dev veth0
ip link del dev veth1
ip link del dev veth2
ip link del dev veth3

ip netns delete client
ip netns delete server1
ip netns delete server2
ip netns delete server3
