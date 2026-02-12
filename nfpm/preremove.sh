#!/bin/sh
set -e
if command -v systemctl >/dev/null 2>&1; then
  systemctl --no-reload disable pipy.service >/dev/null 2>&1 || true
  systemctl stop pipy.service >/dev/null 2>&1 || true
fi
