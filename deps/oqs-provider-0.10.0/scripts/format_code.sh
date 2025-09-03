#!/bin/sh

# SPDX-License-Identifier: MIT

# usage: ./scripts/format_code.sh

arch=$(uname -m)

# tested on Ubuntu 22 / x86_64 and macOS 13 / arm64
if [ "$arch" != "x86_64" ] && [ "$arch" != "arm64" ]
then
	echo "This script does not currently support systems where \`uname -m\` returns $arch."
	exit 1
fi

docker run -u $(id -u ${USER}):$(id -g ${USER}) --rm -v`pwd`:/root/oqsprovider -w /root/oqsprovider openquantumsafe/ci-ubuntu-latest:latest ./scripts/do_code_format.sh --no-dry-run
