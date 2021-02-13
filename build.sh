#!/usr/bin/env bash

##### Default environment variables #########
PIPY_CONF=pipy.cfg

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  PIPY_DIR=$(dirname $(readlink -e $(basename $0)))
else
  PIPY_DIR=`pwd`
fi

TEST_CASE_DIR=${TEST_CASE_DIR:-$PIPY_DIR/test}

# Number of processors to build.
# If you want to define it, please use environment variable NPROC, like: export NPROC=8
__NPROC=${NPROC:-$(getconf _NPROCESSORS_ONLN)}

BUILD_ONLY=false
TEST_ONLY=false
TEST_CASE=all

##### End Default environment variables #########

SHORT_OPTS="bthr:"
LONG_OPTS="test:,help,build-only,test-only"

function usage() {
    echo "Usage: $0 [-h|-b|-t|-r <xxx>]" 1>&2
    echo "       -h           Show this help message"
    echo "       -b           Build only, do not run any test cases"
    echo "       -t           Test only, do not build pipy binary"
    echo "       -r <number>  Run specific test case, with number, like 001"
    echo ""
    exit 1
}

OPTS=$(getopt $SHORT_OPTS "$@")
if [ $? != 0 ] ; then echo "Failed to parse options...exiting." >&2 ; exit 1 ; fi

eval set -- "$OPTS"
while true ; do
  case "$1" in
    -b)
      BUILD_ONLY=true
      shift
      ;;
    -t)
      TEST_ONLY=true
      shift
      ;;
    -r)
      TEST_CASE+="$2 "
      shift 2
      ;;
    -h)
        usage
        ;;
    --)
        shift
        break
        ;;
    *)
        usage
        ;;
  esac
done

shift $((OPTIND-1))
[ $# -ne 0 ] && usage

if $BUILD_ONLY && $TEST_ONLY ; then
  echo $BUILD_ONLY
  echo $TEST_ONLY
  echo "Error: BUILD_ONLY and TEST_ONLY can not both be true simultaneously." 2>&1
  usage
fi

CMAKE=
function __build_deps_check() {
  if [ ! -z $(command -v cmake) ]; then
    export CMAKE=cmake
  elif [ ! -z $(command -v cmake3) ]; then
    export CMAKE=cmake3
  fi
  clang --version 2>&1 > /dev/null && clang++ --version 2>&1 > /dev/null && export __CLANG_EXIST=true
  if [ "x"$CMAKE = "x" ] || ! $__CLANG_EXIST ; then echo "Command \`cmake\` or \`clang\` not found." && exit -1; fi
}

function build() {
  __build_deps_check
  export CC=clang
  export CXX=clang++
  mkdir ${PIPY_DIR}/build 2>&1 > /dev/null || true
  rm -fr ${PIPY_DIR}/build/*
  cd ${PIPY_DIR}/build
  $CMAKE -DCMAKE_BUILD_TYPE=Release $PIPY_DIR
  make -j${__NPROC}
  if [ $? -eq 0 ];then 
    echo "pipy now is in ${PIPY_DIR}/bin"
  fi
  cd - 2>&1 > /dev/null
}

#function __testcases() {
#  if [ "$TEST_CASE" == "all" ]; then
#    __TEST_CASES=`ls -d  [0-9]*`
#  elif [ ! -z $TEST_CASE ]; then
#     __TEST_CASES=
#  fi
#}

function __test() {
  echo "Yet to finalize"
}

if ! $TEST_ONLY ; then
  build
fi

#if [ ! $BUILD_ONLY ]; then
#  if [ ! -z $TEST_CASE ]; then
#    __test() __TEST_CASES
#fi
