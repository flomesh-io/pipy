#!/bin/sh
set -e

export PIPY_SPAWN=${PIPY_SPAWN:-0}

if [[ "$1" == "pipy" ]]; then
  if [[ "$2" == "docker-start" ]]; then
    shift 2
    # workaround for https://github.com/moby/moby/issues/31243
    chmod o+w /proc/self/fd/1 || true
    chmod o+w /proc/self/fd/2 || true

    if [ "${PIPY_CONFIG_FILE}x" == 'x' ]; then
      PIPY_CONFIG_FILE=/etc/pipy/test/001-echo/pipy.js
    fi
    if [ "$(id -u)" != "0" ]; then
      for i in $(seq 1 1 $PIPY_SPAWN); do
        exec /usr/local/bin/pipy ${PIPY_CONFIG_FILE} --reuse-port &
      done
      exec /usr/local/bin/pipy ${PIPY_CONFIG_FILE} --reuse-port
    else
      for i in $(seq 1 1 $PIPY_SPAWN); do
        exec su-exec pipy /usr/local/bin/pipy ${PIPY_CONFIG_FILE} --reuse-port &
      done
      exec su-exec pipy /usr/local/bin/pipy ${PIPY_CONFIG_FILE} --reuse-port 
    fi
  fi
fi

exec "$@"
