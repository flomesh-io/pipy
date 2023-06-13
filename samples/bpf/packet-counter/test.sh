#!/bin/bash

on_exit() {
  ip link set lo xdpgeneric off
}

trap on_exit SIGINT

ip link set lo xdpgeneric obj ../../../bin/packet-counter.o sec xdp

../../../bin/pipy main.js
