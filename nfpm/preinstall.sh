#!/bin/sh
set -e
if command -v getent >/dev/null 2>&1; then
  getent group pipy >/dev/null || groupadd -r pipy
  getent passwd pipy >/dev/null || useradd -r -g pipy -G pipy \
    -d /etc/pipy -s /usr/sbin/nologin -c "pipy" pipy
elif command -v addgroup >/dev/null 2>&1; then
  addgroup -S pipy 2>/dev/null || true
  adduser -S -G pipy -h /etc/pipy -s /sbin/nologin -D pipy 2>/dev/null || true
fi
