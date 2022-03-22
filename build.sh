#!/usr/bin/env bash

##### Default environment variables #########
PIPY_CONF=pipy.js

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

# Number of processors to build.
# If you want to define it, please use environment variable NPROC, like: export NPROC=8
__NPROC=${NPROC:-$(getconf _NPROCESSORS_ONLN)}

BUILD_CONTAINER=false
BUILD_RPM=false
BUILD_BINARY=true

IMAGE_TAG=${IMAGE_TAG:-latest}
DOCKERFILE=${DOCKERFILE:-Dockerfile}

PIPY_STATIC=OFF
PIPY_GUI=${PIPY_GUI:-OFF}

##### End Default environment variables #########

SHORT_OPTS="crsgt:nh"

function usage() {
    echo "Usage: $0 [-h|-c|-r|-s|-g|-n|-t <version-revision>]" 1>&2
    echo "       -h                     Show this help message"
    echo "       -t <version-revision>  Specify release version, like 0.2.0-15, should be one of release tag"
    echo "       -c                     Build container image"
    echo "       -r                     Build RHEL/CentOS rpm package"
    echo "       -s                     Build static binary pipy executable object"
    echo "       -g                     Build pipy with GUI, default with no GUI"
    echo "       -n                     Build pipy binary, default yes"
    echo ""
    exit 1
}

OPTS=$(getopt $SHORT_OPTS "$@")
if [ $? != 0 ] ; then echo "Failed to parse options...exiting." >&2 ; exit 1 ; fi

eval set -- "$OPTS"
while true ; do
  case "$1" in
    -t)
      RELEASE_VERSION="$2"
      shift 2
      ;;
    -c)
      BUILD_CONTAINER=true
      shift
      ;;
    -r)
      BUILD_RPM=true
      shift
      ;;
    -s)
      PIPY_STATIC=ON
      shift
      ;;
    -g)
      PIPY_GUI=ON
      shift
      ;;
    -n)
      BUILD_BINARY=false
      shift
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

function version_compare() {
  if [ "$(printf '%s\n' "$1" "$2" | sort -V | head -n1)" = "$1" ]; then
    true
  else
    return 125
  fi
}

CMAKE=
function __build_deps_check() {
  if [ ! -z $(command -v cmake3) ]; then
    export CMAKE=cmake3
  elif [ ! -z $(command -v cmake) ]; then
    export CMAKE=cmake
  fi
  clang --version 2>&1 > /dev/null && clang++ --version 2>&1 > /dev/null && export __CLANG_EXIST=true
  if [ "x"$CMAKE = "x" ] || ! $__CLANG_EXIST ; then echo "Command \`cmake\` or \`clang\` not found." && exit -1; fi
  __NODE_VERSION=`node --version 2> /dev/null`
  version_compare "v12" "$__NODE_VERSION"
  if [ $? -ne 0 ] && [ $PIPY_GUI == "ON" ] ; then
    echo "NodeJS is too old, the minimal requirement is NodeJS 12."
    exit -1
  fi
}

function build() {
  __build_deps_check
  cd ${PIPY_DIR}
  BRANCH=`git rev-parse --abbrev-ref HEAD`
  git fetch
  git pull origin $BRANCH
  export CC=clang
  export CXX=clang++
  cd ${PIPY_DIR}
  if [ $PIPY_GUI == "ON" ] ; then
    npm install
    npm run build
  fi
  mkdir ${PIPY_DIR}/build 2>&1 > /dev/null || true
  rm -fr ${PIPY_DIR}/build/*
  cd ${PIPY_DIR}/build
  $CMAKE -DPIPY_GUI=${PIPY_GUI} -DPIPY_TUTORIAL=${PIPY_GUI} -DPIPY_STATIC=${PIPY_STATIC} -DCMAKE_BUILD_TYPE=Release $PIPY_DIR
  make -j${__NPROC}
  if [ $? -eq 0 ];then 
    echo "pipy now is in ${PIPY_DIR}/bin"
  fi
  cd - 2>&1 > /dev/null
}

if $BUILD_BINARY; then
  build
fi

# Build RPM from container
if $BUILD_RPM; then
  cd $PIPY_DIR
  RELEASE_VERSION=${RELEASE_VERSION:-latest}
  if [ $RELEASE_VERSION == "latest" ]; then
    git checkout main
    git pull origin main
  else
    git checkout $RELEASE_VERSION
  fi
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

  sudo docker build -t pipy-pjs-rpmbuild:$RELEASE_VERSION \
    --build-arg VERSION=$VERSION \
    --build-arg REVISION=$REVISION \
    --build-arg COMMIT_ID=$COMMIT_ID \
    --build-arg COMMIT_DATE="$COMMIT_DATE" \
    -f $DOCKERFILE .
  sudo docker run -it --rm -v $PIPY_DIR/rpm:/data pipy-pjs-rpmbuild:$RELEASE_VERSION cp /rpm/*.rpm /data
  git checkout -- $PIPY_DIR/rpm/pipy.spec
  rm -f $PIPY_DIR/rpm/pipy.tar.gz
fi

if $BUILD_CONTAINER; then
  cd $PIPY_DIR
  if [ "x"$RELEASE_VERSION != "x" ]; then
    IMAGE_TAG=$RELEASE_VERSION
    COMMIT_ID=$(git log -1 --format=%H)
    COMMIT_DATE=$(git log -1 --format=%cD)
    VERSION=$(echo $RELEASE_VERSION | cut -d\- -f 1)
    REVISION=$(echo $RELEASE_VERSION | cut -d\- -f 2)
    sudo docker build --rm -t pipy-pjs:$IMAGE_TAG \
    --build-arg VERSION=$VERSION \
    --build-arg REVISION=$REVISION \
    --build-arg COMMIT_ID=$COMMIT_ID \
    --build-arg COMMIT_DATE="$COMMIT_DATE" \
    --build-arg PIPY_GUI="$PIPY_GUI" \
    --build-arg PIPY_STATIC="$PIPY_STATIC" \
    -f $DOCKERFILE .
  else
    sudo docker build --rm -t pipy-pjs:$IMAGE_TAG \
    --build-arg PIPY_GUI="$PIPY_GUI" \
    --build-arg PIPY_STATIC="$PIPY_STATIC" \
    -f $DOCKERFILE .
  fi
fi
