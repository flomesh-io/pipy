#!/bin/bash

on_exit() {
  ip link set lo xdpgeneric off
}

trap on_exit SIGINT

ip link set lo xdpgeneric obj ../../../bin/load-balancer.o sec xdp

../../../bin/pipy main.js
