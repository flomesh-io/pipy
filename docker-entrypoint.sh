#!/bin/sh
set -e

function set_pipy_threads() {
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
    export DEFAULT_SPAWN=1
  else
    export DEFAULT_SPAWN=$(($QUOTA/$PERIOD))
  fi

  export PIPY_SPAWN=${PIPY_SPAWN:-$DEFAULT_SPAWN}
  export PIPY_THREADS=${PIPY_THREADS:-$PIPY_SPAWN}

  if [ $PIPY_THREADS -le 1 ]; then
    export THREAD_ARGS=""
  else
    export THREAD_ARGS=" --threads=$PIPY_THREADS --reuse-port "
  fi
}

if [ $(readlink /proc/$$/ns/pid) = $(readlink /proc/1/ns/pid) ]
then
  # in container
  set_pipy_threads

  # workaround for https://github.com/moby/moby/issues/31243
  chmod o+w /proc/self/fd/1 || true
  chmod o+w /proc/self/fd/2 || true
fi

if [[ "$1" == "pipy" || "$1" == "/usr/local/bin/pipy" ]]; then
  if [[ "$2" == "docker-start" ]]; then
    shift 2
    if [ "${PIPY_CONFIG_FILE}x" == 'x' ]; then
      PIPY_CONFIG_FILE=/etc/pipy/tutorial/02-echo/main.js
    fi
    if [ "$(id -u)" != "0" ]; then
      exec /usr/local/bin/pipy $THREAD_ARGS ${PIPY_CONFIG_FILE}
    else
      exec su-exec pipy /usr/local/bin/pipy $THREAD_ARGS ${PIPY_CONFIG_FILE}
    fi
  else
    exec "$@" $THREAD_ARGS
  fi
fi

exec "$@"
