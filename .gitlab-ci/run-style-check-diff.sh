#!/bin/bash

set -e

ancestor_horizon=31  # days (one month)

# Recently, git is picky about directory ownership. Tell it not to worry.
git config --global --add safe.directory "$PWD"

# We need to add a new remote for the upstream target branch, since this script
# could be running in a personal fork of the repository which has out of date
# branches.
#
# Limit the fetch to a certain date horizon to limit the amount of data we get.
# If the branch was forked from origin/main before this horizon, it should
# probably be rebased.
if ! git ls-remote --exit-code upstream >/dev/null 2>&1 ; then
    git remote add upstream https://gitlab.gnome.org/GNOME/gnome-disk-utility.git
fi

# Work out the newest common ancestor between the detached HEAD that this CI job
# has checked out, and the upstream target branch (which will typically be
# `upstream/main` or `upstream/glib-2-62`).
# `${CI_MERGE_REQUEST_TARGET_BRANCH_NAME}` or `${CI_MERGE_REQUEST_SOURCE_BRANCH_NAME}`
# are only defined if we’re running in a merge request pipeline,
# fall back to `${CI_DEFAULT_BRANCH}` or `${CI_COMMIT_BRANCH}` respectively
# otherwise.

source_branch="${CI_MERGE_REQUEST_SOURCE_BRANCH_NAME:-${CI_COMMIT_BRANCH}}"
target_branch="${CI_MERGE_REQUEST_TARGET_BRANCH_NAME:-${CI_DEFAULT_BRANCH}}"

# By default, fetch the source branch from origin
source_remote="origin"

# When running in a fork MR, we need to fetch the source branch from the fork,
# not from origin (which might be the upstream repo where the branch doesn't exist)
if [ -n "${CI_MERGE_REQUEST_SOURCE_PROJECT_URL}" ] && [ "${CI_MERGE_REQUEST_SOURCE_PROJECT_URL}" != "${CI_MERGE_REQUEST_PROJECT_URL}" ]; then
    if ! git ls-remote --exit-code source-project >/dev/null 2>&1 ; then
        git remote add source-project "${CI_MERGE_REQUEST_SOURCE_PROJECT_URL}.git"
    fi
    source_remote="source-project"
fi

git fetch --shallow-since="$(date --date="${ancestor_horizon} days ago" +%Y-%m-%d)" ${source_remote} "${source_branch}"
git fetch --shallow-since="$(date --date="${ancestor_horizon} days ago" +%Y-%m-%d)" upstream "${target_branch}"

newest_common_ancestor_sha=$(git merge-base upstream/${target_branch} ${source_remote}/${source_branch})
if [ -z "${newest_common_ancestor_sha}" ]; then
    echo "Couldn’t find common ancestor with upstream main branch. This typically"
    echo "happens if you branched from main a long time ago. Please update"
    echo "your clone, rebase, and re-push your branch."
    exit 1
fi

git diff -U0 --no-color "${newest_common_ancestor_sha}" | .gitlab-ci/clang-format-diff.py -binary "clang-format" -p1
exit_status=$?

exit ${exit_status}
