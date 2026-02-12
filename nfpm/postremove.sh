#!/bin/sh
# deb purge: $1="purge"; rpm: $1=0; alpine/arch: no args or version
action="$1"
case "$action" in
  purge|0|remove)
    userdel pipy 2>/dev/null || deluser pipy 2>/dev/null || true
    ;;
  *)
    userdel pipy 2>/dev/null || deluser pipy 2>/dev/null || true
    ;;
esac
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload >/dev/null 2>&1 || true
fi
