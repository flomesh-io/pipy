#!/bin/sh

PIPY_DIR=$(cd "$(dirname "$0")" && pwd)

cd "$PIPY_DIR"

if [ -n "$CI_COMMIT_SHA" ]; then
  VERSION="$CI_COMMIT_TAG"
  COMMIT="$CI_COMMIT_SHA"
  COMMIT_DATE="$CI_COMMIT_DATE"
else
  VERSION=`git describe --abbrev=0 --tags`
  COMMIT=`git log -1 --format=%H`
  COMMIT_DATE=`git log -1 --format=%cD`
fi

OLD=`cat $1`

NEW="
#ifndef __VERSION_H__
#define __VERSION_H__

#define PIPY_VERSION \"$VERSION\"
#define PIPY_COMMIT \"$COMMIT\"
#define PIPY_COMMIT_DATE \"$COMMIT_DATE\"

#endif // __VERSION_H__"

if [ "$NEW" != "$OLD" ]; then
  echo "Writing $1..."
  echo "$NEW" > $1;
else
  echo "Version file $1 has no changes"
fi
