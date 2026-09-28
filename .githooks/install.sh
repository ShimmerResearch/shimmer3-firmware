#!/bin/sh
#
# Point git at the hooks checked in under .githooks/. Run once per clone.
#
#   .githooks/install.sh
#
# Undo with:  git config --unset core.hooksPath
set -eu

root=$(git rev-parse --show-toplevel)
git -C "$root" config core.hooksPath .githooks
echo "hooks enabled: $root"

# A submodule is a separate repository: commits made inside it are its commits,
# and need its own hook configuration. Configure every checked-out submodule
# that carries a .githooks of its own - log-and-stream-common, in a Shimmer3 or
# Shimmer3R checkout. Vendor submodules have none and are left alone.
git -C "$root" submodule --quiet foreach --recursive \
  'if [ -d .githooks ]; then git config core.hooksPath .githooks && echo "hooks enabled: $toplevel/$sm_path"; fi'

echo
echo "Staged .c/.h files are now clang-formatted as part of each commit."
echo "Bypass a single commit with: git commit --no-verify"
