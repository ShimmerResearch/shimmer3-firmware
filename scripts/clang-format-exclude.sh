#!/bin/sh
#
# The single reader of .clang-format-exclude.
#
# That file is the only place the formatting scope and the clang-format version
# live; this script is the only place it is interpreted. Everything that needs
# either asks here:
#
#   --sources               print the source roots as ./paths, space separated,
#                           for the source input of DoozyX/clang-format-lint-action
#   --globs                 print the exclusions as fnmatch patterns, space
#                           separated, for the action's exclude input
#   --clang-format-version  print the version CI runs - the only one the
#                           pre-commit hook will format with
#   --filter                read repo-relative paths on stdin, print those that
#                           are formatted: inside a source root, not excluded
#   --list                  print every tracked .c/.h that is formatted
#
# Adding a mode is how you add a consumer. Re-typing the list is not.
#
# This file is identical in every firmware repository that carries it. Change
# it in one and copy it to the rest.
set -eu

root=$(git rev-parse --show-toplevel)
cfg="$root/.clang-format-exclude"

[ -f "$cfg" ] || { echo "$0: $cfg not found" >&2; exit 1; }

case "${1:---list}" in
  --sources|--globs|--clang-format-version|--filter)
    ;;

  --list)
    # Tracked files only, because that is what CI's checkout holds. A
    # submodule's files belong to its own repository and are not listed.
    git -C "$root" -c core.quotePath=false ls-files -- '*.c' '*.h' | "$0" --filter
    exit
    ;;

  *)
    echo "usage: $0 [--sources|--globs|--clang-format-version|--filter|--list]" >&2
    exit 2
    ;;
esac

# One awk program parses the file and answers every mode, so there is exactly
# one reading of the format. The path and mode go in through the environment
# rather than -v, which would expand backslashes in them.
CFE_FILE=$cfg
CFE_MODE=$1
export CFE_FILE CFE_MODE
exec awk '
function fail(msg) {
  printf "%s: %s\n", cfg, msg > "/dev/stderr"
  failed = 1
  exit 1
}

function trim(s) {
  sub(/^[ \t]+/, "", s)
  sub(/[ \t\r]+$/, "", s)
  return s
}

# Tolerate "./dir" and "dir/"; the canonical form has neither.
function tidy(p) {
  sub(/^\.\//, "", p)
  sub(/\/+$/, "", p)
  return p
}

# The prefix the action sees on the files of root i. It walks each source it
# is given and fnmatches every path it meets, and a source of ./A yields paths
# that start ./A/. A pattern with no wildcards therefore matches exactly one
# path, and a directory pattern prunes the whole subtree - the same "equals or
# sits under" rule --filter applies.
function prefix(i) {
  return rootdir[i] == "" ? "./" : "./" rootdir[i] "/"
}

BEGIN {
  cfg = ENVIRON["CFE_FILE"]
  mode = ENVIRON["CFE_MODE"]
  nroot = 0
  nexcl = 0
  version = ""

  # A source-root line opens a section; the paths below it, up to the next
  # source-root line, are exclusions relative to it.
  while ((rc = (getline line < cfg)) > 0) {
    sub(/#.*/, "", line)
    line = trim(line)
    if (line == "")
      continue
    if (line ~ /^source-root:/) {
      rootdir[++nroot] = tidy(trim(substr(line, 13)))
    } else if (line ~ /^clang-format-version:/) {
      if (version != "")
        fail("clang-format-version is given twice")
      version = trim(substr(line, 22))
    } else {
      if (nroot == 0)
        fail("\"" line "\" comes before any source-root line")
      exroot[++nexcl] = nroot
      expath[nexcl] = tidy(line)
    }
  }
  if (rc < 0)
    fail("cannot be read")
  close(cfg)

  if (nroot == 0)
    fail("has no source-root line")
  if (version == "")
    fail("has no clang-format-version line")
  # A file under two roots would be walked twice by CI, and could be excluded
  # by one section and not the other.
  for (i = 1; i <= nroot; i++)
    for (j = 1; j <= nroot; j++)
      if (i != j && (rootdir[i] == "" || index(rootdir[j] "/", rootdir[i] "/") == 1))
        fail("source roots \"" rootdir[i] "\" and \"" rootdir[j] "\" overlap")

  if (mode == "--clang-format-version") {
    print version
    exit
  }
  if (mode == "--sources") {
    out = ""
    for (i = 1; i <= nroot; i++)
      out = out " " (rootdir[i] == "" ? "." : "./" rootdir[i])
    print substr(out, 2)
    exit
  }
  if (mode == "--globs") {
    out = ""
    for (k = 1; k <= nexcl; k++)
      out = out " " prefix(exroot[k]) expath[k]
    print substr(out, 2)
    exit
  }
  # --filter reads the paths on stdin, below.
}

{
  f = $0
  sub(/\r$/, "", f)
  if (f == "")
    next

  # Roots do not overlap, so the first one a path is under is its only one.
  for (i = 1; i <= nroot; i++) {
    if (rootdir[i] == "") {
      rel = f
      break
    }
    if (index(f, rootdir[i] "/") == 1) {
      rel = substr(f, length(rootdir[i]) + 2)
      break
    }
  }
  if (i > nroot)
    next    # outside every source root: not ours to format

  # Excluded when it equals an entry of its own section or sits under one.
  # Prefix matching on real paths - no globs, nothing to get subtly wrong.
  for (k = 1; k <= nexcl; k++)
    if (exroot[k] == i && (rel == expath[k] || index(rel, expath[k] "/") == 1))
      next

  print f
}

END {
  if (failed)
    exit 1
}
'
