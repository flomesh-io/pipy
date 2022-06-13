#!/bin/sh
set -e

function set_pipy_spawn() {
  # cgroup v1
  if [ -f "/sys/fs/cgroup/cpu/cpu.cfs_quota_us" ]
  then
    QUOTA=$(cat /sys/fs/cgroup/cpu/cpu.cfs_quota_us)
    PERIOD=$(cat /sys/fs/cgroup/cpu/cpu.cfs_period_us)
  fi

  # cgroup v2
  if [ -f "/sys/fs/cgroup/cpu.max" ]
  then
    QUOTA=$(cut -d ' ' -f 1 /sys/fs/cgroup/cpu.max)
    PERIOD=$(cut -d ' ' -f 2 /sys/fs/cgroup/cpu.max)
  fi

  if [ "$QUOTA" = "-1" ] || [ "$QUOTA" = "max" ]
  then
    export DEFAULT_SPAWN=0
  else
    export DEFAULT_SPAWN=$(($QUOTA/$PERIOD - 1))
  fi

  export PIPY_SPAWN=${PIPY_SPAWN:-$DEFAULT_SPAWN}
}

if [ $(readlink /proc/$$/ns/pid) = $(readlink /proc/1/ns/pid) ]
then
  # in container
  set_pipy_spawn

  # workaround for https://github.com/moby/moby/issues/31243
  chmod o+w /proc/self/fd/1 || true
  chmod o+w /proc/self/fd/2 || true
fi

if [[ "$1" == "pipy" ]]; then
  if [[ "$2" == "docker-start" ]]; then
    shift 2
    if [ "${PIPY_CONFIG_FILE}x" == 'x' ]; then
      PIPY_CONFIG_FILE=/etc/pipy/tutorial/02-echo/hello.js
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
  else
    for i in $(seq 1 1 $PIPY_SPAWN); do
      exec "$@" --reuse-port &
    done
    exec "$@" --reuse-port
  fi
fi

exec "$@"
