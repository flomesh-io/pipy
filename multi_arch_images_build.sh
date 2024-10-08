#!/usr/bin/env bash
#set -eu pipefail

PIPY_STATIC=OFF
PIPY_GUI=${PIPY_GUI:-OFF}

function usage() {
    echo "Usage: $0 [-h|-t <version-revision>]" 1>&2
    echo "       -h                     Show this help message"
    echo "       -t <version-revision>  Specify the release version (should be one of the release tags, e.g. 0.2.0-15)"
    echo "       -s                     Build with static linking (default: no)"
    echo "       -g                     Build with the builtin GUI (default: no)"
    echo ""
    exit 1
}

# 解析选项
while getopts ":ht:sg" opt; do
  case "${opt}" in
    t)
      RELEASE_VERSION="$OPTARG"
      ;;
    s)
      PIPY_STATIC=ON
      ;;
    g)
      PIPY_GUI=ON
      ;;
    h)
      usage
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



# 设置 Docker 参数
DOCKERFILE=${DOCKERFILE:-Dockerfile}
IMAGE=${IMAGE:-flomesh/pipy}

# 获取镜像标签
if [[ "$RELEASE_VERSION" != "nightly"* ]]; then
    IMAGE_TAG=$RELEASE_VERSION
else
    IMAGE_TAG=${RELEASE_VERSION##nightly-}
    IMAGE=${IMAGE}-nightly
fi

# 输出构建命令
echo "Building image ${IMAGE}:${IMAGE_TAG} for multiple architectures..."

# 定义要构建的架构
ARCHS=("linux/amd64" "linux/arm64" "linux/arm/v7" "linux/arm/v6" "linux/386" "linux/ppc64le" "linux/s390x" "linux/riscv64")

# 循环构建每个架构的镜像
for arch in "${ARCHS[@]}"; do
    echo "Building for architecture: $arch"
    arch_name=$(echo $arch | sed 's|linux/||' | sed 's|/|-|g')  # 去掉前缀linux/并替换/为-

    # 特别处理arm/v7和arm/v6
    if [[ $arch == "linux/arm/v7" ]]; then
        arch_name="arm-v7"
    elif [[ $arch == "linux/arm/v6" ]]; then
        arch_name="arm-v6"
    fi

    # 构建镜像
    docker buildx build --platform $arch -t ${IMAGE}:${IMAGE_TAG}-$arch_name \
        --network=host \
        --build-arg VERSION=$VERSION \
        --build-arg REVISION=$REVISION \
        --build-arg COMMIT_ID=$COMMIT_ID \
        --build-arg COMMIT_DATE="$COMMIT_DATE" \
        --build-arg PIPY_GUI="$PIPY_GUI" \
        --build-arg PIPY_STATIC="$PIPY_STATIC" \
        --build-arg BUILD_TYPE="$BUILD_TYPE" \
        --push \
        -f $DOCKERFILE .
done

# 创建多架构镜像
MULTI_ARCH_TAGS=()
for arch in "${ARCHS[@]}"; do
    arch_name=$(echo $arch | sed 's|linux/||' | sed 's|/|-|g')
    if [[ $arch == "linux/arm/v7" ]]; then
        arch_name="arm-v7"
    elif [[ $arch == "linux/arm/v6" ]]; then
        arch_name="arm-v6"
    fi
    MULTI_ARCH_TAGS+=("${IMAGE}:${IMAGE_TAG}-${arch_name}")
done

echo "Creating multi-architecture image: ${IMAGE}:${IMAGE_TAG}"
docker buildx imagetools create "${MULTI_ARCH_TAGS[@]}" -t "${IMAGE}:${IMAGE_TAG}"

echo "Build and packaging complete."
docker buildx imagetools inspect "${IMAGE}:${IMAGE_TAG}"
