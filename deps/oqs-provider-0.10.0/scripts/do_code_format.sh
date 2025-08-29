#!/bin/bash
# SPDX-License-Identifier: MIT

rv=0

if [ "$1" = "--no-dry-run" ]; then
	dryrun="-i"
else
	dryrun="--dry-run"
fi

ep=`pwd`
find oqsprov test -type f -and -name '*.[ch]' | xargs clang-format --style="{BasedOnStyle: llvm, IndentWidth: 4}" $dryrun --Werror
if [ $? -ne 0 ]; then
   echo "Error: Some files need reformatting. Check output above."
   rv=-1
fi

# check _all_ source files for CRLF line endings:
find oqsprov test -name '*.[chS]' -exec file "{}" ";" | grep CRLF
if [ $? -ne 1 ]; then
   echo "Error: Files found with non-UNIX line endings."
   echo "To fix, consider running \"find oqsprov test -name '*.[chS]' | xargs sed -i 's/\r//' \"."
   rv=-1
fi

exit $rv
