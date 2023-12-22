#!/bin/bash

ip link set lo xdpgeneric off
ip link set veth0 xdpgeneric off
ip link set veth1 xdpgeneric off
ip link set veth2 xdpgeneric off
ip link set veth3 xdpgeneric off

ip link del dev veth0
ip link del dev veth1
ip link del dev veth2
ip link del dev veth3

ip netns delete client
ip netns delete server1
ip netns delete server2
ip netns delete server3

rm /sys/fs/bpf/lb
