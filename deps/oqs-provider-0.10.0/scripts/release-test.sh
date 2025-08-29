#!/bin/bash

# Stop in case of error
set -e

# Wrapper around the release-test-ci.sh script to preserve uncommitted modifications.

# back up git status and checkout a fresh branch with identical staged/unstaged changes
save_local_git() {
    # git stash does not have an --allow-empty option, so make sure we have something to stash.
    # This allows us to safely call git stash pop.
    tmpfile=$(mktemp ./XXXXXX)
    git add $tmpfile
    # back up uncommitted changes
    git stash push --quiet
    # restore changes but save stash
    git stash apply --quiet
    # delete dummy file
    git rm -f $tmpfile --quiet
    # save working branch name
    working_branch=$(git branch --show-current)
    # checkout a fresh branch
    reltest_branch="reltest-$RANDOM"
    git checkout -b $reltest_branch --quiet
}

# restore git status
restore_local_git() {
    # switch back to working branch; delete temporary branch; reset to HEAD; pop stashed changes; delete dummy file
    git switch $working_branch --quiet && git branch -D $reltest_branch --quiet && git reset --hard --quiet && git stash pop --quiet && git rm -f $tmpfile --quiet
}

save_local_git
trap restore_local_git EXIT
# clean out the build directory and run tests
rm -rf _build
./scripts/release-test-ci.sh
