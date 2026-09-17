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

# The shared library is a separate repository. Editing it from inside a firmware
# checkout makes commits in ITS repo, not the parent's, so it needs its own hook
# configuration - one clone, two places to set this. Harmless when the submodule
# is absent or not checked out.
for sub in "$root"/LogAndStream_Shimmer3*/log-and-stream-common; do
  [ -e "$sub/.git" ] || continue
  [ -d "$sub/.githooks" ] || continue
  git -C "$sub" config core.hooksPath .githooks
  echo "hooks enabled: $sub"
done

echo
echo "Staged .c/.h files are now clang-formatted as part of each commit."
echo "Bypass a single commit with: git commit --no-verify"
