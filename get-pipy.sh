#!/usr/bin/env bash

# Copyright 2025 the flomesh.io Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# The install script is based off of the Apache 2.0-licensed script from helm,
# https://github.com/helm/helm/blob/main/scripts/get-helm-3

: ${BINARY_NAME:="pipy"}
: ${USE_SUDO:="true"}
: ${PIPY_INSTALL_DIR:="/usr/local/bin"}
: ${DEBUG:="false"}

HAS_CURL="$(type "curl" &> /dev/null && echo true || echo false)"
HAS_WGET="$(type "wget" &> /dev/null && echo true || echo false)"
HAS_GIT="$(type "git" &> /dev/null && echo true || echo false)"
HAS_TAR="$(type "tar" &> /dev/null && echo true || echo false)"

# initArch discovers the architecture for this system.
initArch() {
  ARCH=$(uname -m)
  case $ARCH in
    arm64) ARCH="aarch64";;
    aarch64) ARCH="aarch64";;
    amd64) ARCH="x86_64";;
    x86_64) ARCH="x86_64";;
  esac
}

# initOS discovers the operating system for this system.
initOS() {
  OS=$(echo `uname`|tr '[:upper:]' '[:lower:]')

  case "$OS" in
    # Minimalist GNU for Windows
    mingw*|cygwin*) OS='windows';;
    linux) OS='generic_linux';;
    darwin) OS='macos';;
  esac
}

# runs the given command as root (detects if we are root already)
runAsRoot() {
  if [ $EUID -ne 0 -a "$USE_SUDO" = "true" ]; then
    sudo "${@}"
  else
    "${@}"
  fi
}

# verifySupported checks that the os/arch combination is supported for
# binary builds, as well whether or not necessary tools are present.
verifySupported() {
  local supported="macos-aarch64\nmacos-x86_64\ngeneric_linux-aarch64\ngeneric_linux-x86_64"
  if ! echo "${supported}" | grep -q "${OS}-${ARCH}"; then
    echo "No prebuilt binary for ${OS}-${ARCH}."
    echo "To build from source, go to https://github.com/flomesh-io/pipy"
    exit 1
  fi

  if [ "${HAS_CURL}" != "true" ] && [ "${HAS_WGET}" != "true" ]; then
    echo "Either curl or wget is required"
    exit 1
  fi

  if [ "${HAS_GIT}" != "true" ]; then
    echo "[WARNING] Could not find git. It is required for plugin installation."
    exit 1
  fi

  if [ "${HAS_TAR}" != "true" ]; then
    echo "[ERROR] Could not find tar. It is required to extract the pipy binary archive."
    exit 1
  fi
}

# checkDesiredVersion checks if the desired version is available.
checkDesiredVersion() {
  if [ "x$DESIRED_VERSION" == "x" ]; then
    # Get tag from release URL
    local latest_release_url="https://github.com/flomesh-io/pipy/releases/latest"

    if [ "${HAS_CURL}" == "true" ]; then
      version=$(curl -Ls -o /dev/null -w %{url_effective} "$latest_release_url")
      TAG=${version##*/}
    elif [ "${HAS_WGET}" == "true" ]; then
      version=$(wget -O /dev/null "$latest_release_url" 2>&1 | grep -w 'Location' | cut -d ' ' -f2)
      TAG=${version##*/}
    fi
  else
    TAG=$DESIRED_VERSION
  fi
}

# downloadFile downloads the binary package of specified version
downloadFile() {
  PIPY_DIST="pipy-$TAG-$OS-$ARCH.tar.gz"
  DOWNLOAD_URL="https://github.com/flomesh-io/pipy/releases/download/$TAG/$PIPY_DIST"
  PIPY_TMP_ROOT="$(mktemp -dt pipy-installer-XXXXXX)"
  PIPY_TMP_FILE="$PIPY_TMP_ROOT/$PIPY_DIST"

  echo "Downloading $DOWNLOAD_URL"
  if [ "${HAS_CURL}" == "true" ]; then
    curl -SsL "$DOWNLOAD_URL" -o "$PIPY_TMP_FILE"
  elif [ "${HAS_WGET}" == "true" ]; then
    wget -q -O "$PIPY_TMP_FILE" "$DOWNLOAD_URL"
  fi
}

# installFile installs the Helm binary.
installFile() {
  PIPY_TMP="$PIPY_TMP_ROOT/$BINARY_NAME"
  mkdir -p "$PIPY_TMP"
  tar xf "$PIPY_TMP_FILE" -C "$PIPY_TMP"
  ls -l "$PIPY_TMP"
  PIPY_TMP_BIN="$PIPY_TMP/usr/local/bin/pipy"
  ls -l $PIPY_TMP_BIN
  echo "Preparing to install $BINARY_NAME into ${PIPY_INSTALL_DIR}"
  runAsRoot cp "$PIPY_TMP_BIN" "$PIPY_INSTALL_DIR/$BINARY_NAME"
  echo "$BINARY_NAME installed into $PIPY_INSTALL_DIR/$BINARY_NAME"
}

# fail_trap is executed if an error occurs.
fail_trap() {
  result=$?
  if [ "$result" != "0" ]; then
    if [[ -n "$INPUT_ARGUMENTS" ]]; then
      echo "Failed to install $BINARY_NAME with the arguments provided: $INPUT_ARGUMENTS"
      help
    else
      echo "Failed to install $BINARY_NAME"
    fi
    echo -e "\tFor support, go to https://github.com/flomesh-io/pipy"
  fi
  cleanup
  exit $result
}

# testVersion tests the installed client to make sure it is working.
testVersion() {
  set +e
  PIPY="$(command -v $BINARY_NAME)"
  if [ "$?" = "1" ]; then
    echo "$BINARY_NAME not found. Is $PIPY_INSTALL_DIR on your "'$PATH?'
    exit 1
  fi
  set -e
}

# help provides possible cli installation arguments
help () {
  echo "Accepted cli arguments are:"
  echo -e "\t[--help|-h ] ->> prints this help"
  echo -e "\t[--version|-v <desired_version>] . When not defined it fetches the latest release tag from the pipy repository"
  echo -e "\te.g. --version 1.0.0"
  echo -e "\t[--no-sudo]  ->> install without sudo"
}

# cleanup temporary files
cleanup() {
  if [[ -d "${PIPY_TMP_ROOT:-}" ]]; then
    rm -rf "$PIPY_TMP_ROOT"
  fi
}

# Execution

#Stop execution on any error
trap "fail_trap" EXIT
set -e

# Set debug if desired
if [ "${DEBUG}" == "true" ]; then
  set -x
fi

# Parsing input arguments (if any)
export INPUT_ARGUMENTS="${@}"
set -u
while [[ $# -gt 0 ]]; do
  case $1 in
    '--version'|-v)
       shift
       if [[ $# -ne 0 ]]; then
           export DESIRED_VERSION="${1}"
       else
           echo -e "Please provide the desired version. e.g. --version 3.0.0"
           exit 0
       fi
       ;;
    '--no-sudo')
       USE_SUDO="false"
       ;;
    '--help'|-h)
       help
       exit 0
       ;;
    *) exit 1
       ;;
  esac
  shift
done
set +u

initArch
initOS
verifySupported
checkDesiredVersion
downloadFile
installFile
testVersion
cleanup
