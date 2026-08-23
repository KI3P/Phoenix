#!/bin/bash
#
# One-time setup for a fresh clone. Safe to re-run.
#
# Git does not install hooks on clone and has no way to make it happen
# automatically, so this has to be run by hand once per working copy:
#
#     ./hooks/install.sh
#

set -e

cd "$(git rev-parse --show-toplevel)"

# Point git at the versioned hooks directory instead of .git/hooks.
#
# Note this *replaces* .git/hooks wholesale rather than adding to it - any
# hooks you have installed there yourself will stop running. This repository
# ships only pre-commit, so move anything of your own into hooks/ alongside it.
if [ -n "$(ls -A .git/hooks 2>/dev/null | grep -v '\.sample$')" ]; then
    echo "Note: .git/hooks still contains:"
    ls -A .git/hooks | grep -v '\.sample$' | sed 's/^/    /'
    echo "      These will no longer run. Move any you want to keep into hooks/."
fi

git config core.hooksPath hooks
echo "core.hooksPath   -> hooks"

# BuildInfo.h is restamped by the pre-commit hook on every commit, so every
# branch touches it and merges between branches would conflict on it. The
# .gitattributes entry marks it merge=ours, which needs this driver defined to
# take effect. "true" is the shell builtin: it succeeds without doing anything,
# which leaves the file as-is - correct here, because the next commit restamps
# it regardless of which side won.
git config merge.ours.driver true
echo "merge.ours.driver -> true"

echo
echo "Done. The pre-commit hook now stamps code/src/PhoenixSketch/BuildInfo.h"
echo "with the parent commit and stages it, so the working tree stays clean."
