#!/usr/bin/env bash

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  PIPY_DIR=$(dirname $(readlink -e $(basename $0)))
  OS_NAME=generic_linux
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
  OS_NAME=macos
  export SDKROOT=$(xcrun --show-sdk-path)
fi

# Number of processors to build.
# If you want to define it, please use environment variable NPROC, like: export NPROC=8
__NPROC=${NPROC:-$(getconf _NPROCESSORS_ONLN)}

BUILD_CONTAINER=false
BUILD_RPM=false
BUILD_BINARY=true
BUILD_ANDROID=false
BUILD_TYPE=Release
PACKAGE_OUTPUTS=false

DOCKERFILE=${DOCKERFILE:-Dockerfile}
IMAGE=${IMAGE:-flomesh/pipy}

PKG_NAME=${PKG_NAME:-pipy}

PIPY_STATIC=OFF
PIPY_GUI=${PIPY_GUI:-OFF}

OS_ARCH=$(uname -m | sed 's#arm64#aarch64#g')
##### End Default environment variables #########

SHORT_OPTS="crsgt:nhpda"

function usage() {
    echo "Usage: $0 [-h|-c|-r|-s|-g|-n|-t <version-revision>]" 1>&2
    echo "       -h                     Show this help message"
    echo "       -t <version-revision>  Specify the release version (should be one of the release tags, e.g. 0.2.0-15)"
    echo "       -c                     Build a container image"
    echo "       -a                     Build a android binary"
    echo "       -r                     Build a CentOS/RHEL RPM package"
    echo "       -n                     Build a stand-alone executable (default: yes)"
    echo "       -d                     Build with debugging information (default: no)"
    echo "       -s                     Build with static linking (default: no)"
    echo "       -g                     Build with the builtin GUI (default: no)"
    echo "       -p                     Package the build outputs (default: no)"
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
    -p)
      PACKAGE_OUTPUTS=true
      shift
      ;;
    -d)
      BUILD_TYPE="Debug"
      shift
      ;;
    -a)
      BUILD_ANDROID=true
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

# define release
if git rev-parse --is-inside-work-tree > /dev/null 2>&1
then
    if [ -z "$RELEASE_VERSION" ]
    then
        RELEASE_VERSION=`git name-rev --tags --name-only $(git rev-parse HEAD)`
        if [ $RELEASE_VERSION = 'undefined' ]
        then
            RELEASE_VERSION=nightly-$(date +%Y%m%d%H%M)
        fi
    elif [ $RELEASE_VERSION == "latest" ]; then
      git checkout main
      git pull origin main
    elif [[ $RELEASE_VERSION != "nightly"* ]]; then
      git checkout $RELEASE_VERSION
      if [ $? -ne 0 ]; then
        echo "Cannot find tag $RELEASE_VERSION"
        exit -1
      fi
    fi

    export COMMIT_ID=$(git log -1 --format=%H)
    export COMMIT_DATE=$(git log -1 --format=%cD)
    export VERSION=$(echo $RELEASE_VERSION | cut -d\- -f 1)
    export REVISION=$(echo $RELEASE_VERSION | cut -d\- -f 2)
else
    export RELEASE_VERSION=$(basename ${PIPY_DIR})
    export COMMIT_ID="N/A"
    export COMMIT_DATE="N/A"
    export VERSION=$(basename ${PIPY_DIR})
    export REVISION=
fi

export CI_COMMIT_TAG=$RELEASE_VERSION
export CI_COMMIT_SHA=$COMMIT_ID
export CI_COMMIT_DATE=$COMMIT_DATE

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

  export CC=${CC:-clang}
  export CXX=${CXX:-clang++}

  $CC --version 2>&1 > /dev/null && $CXX --version 2>&1 > /dev/null && export __CLANG_EXIST=true
  if [ "x"$CMAKE = "x" ] || ! $__CLANG_EXIST ; then echo "Command \`cmake\` or \`clang\` not found." && exit -1; fi
  __NODE_VERSION=`node --version 2> /dev/null`
  version_compare "v12" "$__NODE_VERSION"
  if [ $? -ne 0 ] && [ $PIPY_GUI == "ON" ] ; then
    echo "Your current version of Node.js is too old."
    echo "Node.js version 12 or above is required to build Pipy."
    exit -1
  fi
}

function build() {
  __build_deps_check

  cd ${PIPY_DIR}
  if [ $PIPY_GUI == "ON" ] ; then
    npm install
    npm run build
  fi
  mkdir ${PIPY_DIR}/build 2>&1 > /dev/null || true
  rm -fr ${PIPY_DIR}/build/*
  cd ${PIPY_DIR}/build
  $CMAKE -DPIPY_GUI=${PIPY_GUI} -DPIPY_CODEBASES=${PIPY_GUI} -DPIPY_STATIC=${PIPY_STATIC} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} $PIPY_DIR
  make -j${__NPROC}
  if [ $? -eq 0 ];then
    echo "Pipy has been built successfully and can be found in ${PIPY_DIR}/bin"
  fi
  cd - 2>&1 > /dev/null
}

if $BUILD_BINARY; then
  build
  if $PACKAGE_OUTPUTS; then
    mkdir -p ${PIPY_DIR}/buildroot/usr/local/bin
    cp -a ${PIPY_DIR}/bin/pipy ${PIPY_DIR}/buildroot/usr/local/bin/pipy
    tar zcv -f ${PKG_NAME}-${RELEASE_VERSION}-${OS_NAME}-${OS_ARCH}.tar.gz -C ${PIPY_DIR}/buildroot usr/local/bin/pipy
    rm -rf ${PIPY_DIR}/buildroot
  fi
fi

# Build RPM from container
if $BUILD_RPM; then
  cd $PIPY_DIR

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

  sudo docker build -t pipy-rpmbuild:$RELEASE_VERSION \
    --build-arg VERSION=$VERSION \
    --build-arg REVISION=$REVISION \
    --build-arg COMMIT_ID=$COMMIT_ID \
    --build-arg COMMIT_DATE="$COMMIT_DATE" \
    --build-arg PIPY_GUI="$PIPY_GUI" \
    --build-arg PIPY_STATIC="$PIPY_STATIC" \
    --build-arg BUILD_TYPE="$BUILD_TYPE" \
    -f $DOCKERFILE .

  sudo docker run --rm -v $PIPY_DIR/rpm:/data pipy-rpmbuild:$RELEASE_VERSION bash -c "cp /rpm/*.rpm /data"
  git checkout -- $PIPY_DIR/rpm/pipy.spec
  rm -f $PIPY_DIR/rpm/pipy.tar.gz
fi

if $BUILD_CONTAINER; then
  cd $PIPY_DIR
  if [[ "$RELEASE_VERSION" != "nightly"* ]]; then
    IMAGE_TAG=$RELEASE_VERSION
  else
    IMAGE_TAG=${RELEASE_VERSION##nightly-}
    IMAGE=${IMAGE}-nightly
  fi

  echo "Build image ${IMAGE}:$IMAGE_TAG"
  sudo docker build --rm -t ${IMAGE}:$IMAGE_TAG \
    --network=host \
    --build-arg VERSION=$VERSION \
    --build-arg REVISION=$REVISION \
    --build-arg COMMIT_ID=$COMMIT_ID \
    --build-arg COMMIT_DATE="$COMMIT_DATE" \
    --build-arg PIPY_GUI="$PIPY_GUI" \
    --build-arg PIPY_STATIC="$PIPY_STATIC" \
    --build-arg BUILD_TYPE="$BUILD_TYPE" \
    -f $DOCKERFILE .

  if $PACKAGE_OUTPUTS; then
    docker inspect ${IMAGE}:$IMAGE_TAG > /dev/null 2>&1
    if [ $? -ne 0 ]; then
      echo "Build image ${IMAGE}:$IMAGE_TAG failed"
      exit -1
    fi
    docker save ${IMAGE}:$IMAGE_TAG | gzip > ${IMAGE##flomesh/}-${IMAGE_TAG}-alpine-${OS_ARCH}.tar.gz
  fi
fi

if $BUILD_ANDROID; then
  cd $PIPY_DIR

  if [ -z "$NDK"  ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]
  then
    echo "Can't find NDK, exists..."
    exit 1
  fi

  export ANDROID_NDK_ROOT=$NDK

  cd $PIPY_DIR/deps/openssl-3.2.0

  mkdir -p android && cd android

  ANDROID_TARGET_API=34
  ANDROID_TARGET_ABI=arm64-v8a

  OUTPUT=${PWD}/${ANDROID_TARGET_ABI}

  PATH=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
  ../config android-arm64 -D__ANDROID_API__=${ANDROID_TARGET_API} -static no-asm no-shared no-tests --prefix=${OUTPUT}

  make
  make install_sw

  cd $PIPY_DIR
  rm -rf build && mkdir build
  cd $PIPY_DIR/build

  cmake -DCMAKE_TOOLCHAIN_FILE=${NDK}/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 -DCMAKE_ANDROID_STL_TYPE=c++_static \
    -DANDROID_ALLOW_UNDEFINED_SYMBOLS=TRUE \
    -DPIPY_OPENSSL=${PIPY_DIR}/deps/openssl-3.2.0/android/arm64-v8a  \
    -DPIPY_USE_SYSTEM_ZLIB=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE=-g0 \
    -DPIPY_LTO=OFF -DPIPY_GUI=OFF -DPIPY_CODEBASES=OFF \
    -DZLIB_LIBRARY=/usr/lib/x86_64-linux-gnu/libz.a -DZLIB_INCLUDE_DIR=/usr/lib/x86_64-linux-gnu -GNinja ..

  ninja || exit $?

  if $PACKAGE_OUTPUTS; then
    cd $PIPY_DIR

    test -d usr/local/lib || mkdir -p usr/local/lib
    test -d usr/local/bin || mkdir -p usr/local/bin

    cp $ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so usr/local/lib
    cp bin/pipy usr/local/bin/pipy

    tar zcvf \
      ${PKG_NAME}-${RELEASE_VERSION}-android-${ANDROID_TARGET_ABI/-/_}.tar.gz \
      usr/local/lib/libc++_shared.so \
      usr/local/bin/pipy
  fi
fi
