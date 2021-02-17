#!/usr/bin/env bash

##### Default environment variables #########
PIPY_CONF=pipy.cfg

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  PIPY_DIR=$(dirname $(readlink -e $(basename $0)))
elif [[ "$OSTYPE" == "darwin"* ]]; then
  cd `dirname $0`
  TARGET_FILE=`basename $0`
  while [ -L "$TARGET_FILE" ]
  do
    TARGET_FILE=$(readlink $TARGET_FILE)
    cd $(dirname $TARGET_FILE)
    TARGET_FILE=$(basename $TARGET_FILE)
  done
  PHYS_DIR=$(pwd -P)
  RESULT=$PHYS_DIR/$TARGET_FILE
  PIPY_DIR=$(dirname $RESULT)
fi

TEST_CASE_DIR=${TEST_CASE_DIR:-$PIPY_DIR/test}

# Number of processors to build.
# If you want to define it, please use environment variable NPROC, like: export NPROC=8
__NPROC=${NPROC:-$(getconf _NPROCESSORS_ONLN)}

BUILD_ONLY=false
BUILD_CONTAINER=false
BUILD_RPM=false
TEST_ONLY=false
TEST_CASE=all

IMAGE_TAG=latest
DOCKERFILE=Dockerfile

##### End Default environment variables #########

SHORT_OPTS="bthcr:p:"

function usage() {
    echo "Usage: $0 [-h|-b|-c|-p <xxx>|-t|-r <xxx>]" 1>&2
    echo "       -h                     Show this help message"
    echo "       -b                     Build only, do not run any test cases"
    echo "       -c                     Build container image"
    echo "       -p <version-revision>  Build RHEL/CentOS rpm package, like 0.2.0-15, should be one
    of release tag"
    echo "       -t                     Test only, do not build pipy binary"
    echo "       -r <number>            Run specific test case, with number, like 001"
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
    -c)
      BUILD_CONTAINER=true
      shift
      ;;
    -p)
      BUILD_RPM=true
      RELEASE_VERSION="$2"
      shift 2
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

if ! $TEST_ONLY && $BUILD_CONTAINER; then
  sudo docker build --rm -t pipy:$IMAGE_TAG -f $DOCKERFILE $PIPY_DIR
fi

# Build RPM from container
if ! $TEST_ONLY && $BUILD_RPM; then
  cd $PIPY_DIR
  git checkout $RELEASE_VERSION
  if [ $? -ne 0 ]; then
    echo "Cannot find tag $RELEASE_VERSION"
    exit -1
  fi

  COMMIT_ID=$(git log -1 --format=%H)
  COMMIT_DATE=$(git log -1 --format=%cD)
  VERSION=$(echo $RELEASE_VERSION | cut -d\- -f 1)
  REVISION=$(echo $RELEASE_VERSION | cut -d\- -f 2)
 
  __CHANGELOG=`mktemp`
  git log --format="* %cd %aN%n- (%h) %s%d%n" --date=local | sed -r 's/[0-9]+:[0-9]+:[0-9]+ //' > $__CHANGELOG
  cat $__CHANGELOG

  if [ ! -s $__CHANGELOG ]; then
    echo "Cannot parse change log"
    exit -1
  fi

  cd ..
  tar zcvf pipy.tar.gz pipy
  mv pipy.tar.gz $PIPY_DIR/rpm

  cd $PIPY_DIR/rpm
  cat $__CHANGELOG >> pipy.spec
  rm -f $__CHANGELOG

  sudo docker build -t pipy-rpm:$RELEASE_VERSION \
    --build-arg VERSION=$VERSION \
    --build-arg REVISION=$REVISION \
    --build-arg COMMIT_ID=$COMMIT_ID \
    --build-arg COMMIT_DATE="$COMMIT_DATE" \
    -f $DOCKERFILE .
  sudo docker run -it --rm -v $PIPY_DIR/rpm:/data pipy-rpm:$RELEASE_VERSION cp /rpm/pipy-community-${RELEASE_VERSION}.el7.x86_64.rpm /data
  git checkout -- $PIPY_DIR/rpm/pipy.spec
  rm -f $PIPY_DIR/rpm/pipy.tar.gz
fi

#if [ ! $BUILD_ONLY ]; then
#  if [ ! -z $TEST_CASE ]; then
#    __test() __TEST_CASES
#fi
