#!/bin/sh

if [ -n "$CI_COMMIT_SHA" ]; then
  VERSION="$CI_COMMIT_TAG"
  COMMIT="$CI_COMMIT_SHA"
  COMMIT_DATE="$CI_COMMIT_DATE"
else
  VERSION="0.0.0"
  COMMIT=`git log -1 --format=%H`
  COMMIT_DATE=`git log -1 --format=%cD`
fi

echo "
#ifndef __VERSION_H__
#define __VERSION_H__

#define PIPY_VERSION \"$VERSION\"
#define PIPY_COMMIT \"$COMMIT\"
#define PIPY_COMMIT_DATE \"$COMMIT_DATE\"

#endif // __VERSION_H__
" > $1
