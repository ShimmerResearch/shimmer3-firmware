#!/bin/sh
#
# The single reader of .clang-format-exclude.
#
# That file is the only place the exclusion list lives; this script is the only
# place it is interpreted. Everything that needs the list asks here:
#
#   --source-root   print the project directory clang-format runs over
#   --globs         print the list as fnmatch patterns, space separated, for the
#                   exclude input of DoozyX/clang-format-lint-action
#   --filter        read repo-relative paths on stdin, print those NOT excluded
#   --list          print every .c/.h under the source root that IS formatted
#
# Adding a mode is how you add a consumer. Re-typing the list is not.
set -eu

root=$(git rev-parse --show-toplevel)
cfg="$root/.clang-format-exclude"

[ -f "$cfg" ] || { echo "$0: $cfg not found" >&2; exit 1; }

# Strip comments and blank lines once; everything below works off this.
entries=$(sed -e "s/#.*$//" -e "s/[[:space:]]*$//" "$cfg" | grep -v "^$" || true)

source_root=$(printf "%s\\n" "$entries" | sed -n "s/^source-root:[[:space:]]*//p")
paths=$(printf "%s\\n" "$entries" | grep -v "^source-root:" || true)

case "${1:---list}" in
  --source-root)
    printf "%s\\n" "$source_root"
    ;;

  --globs)
    # The action fnmatches each pattern against the path it is walking, and
    # fnmatch does not treat / specially, so a leading */ anchors the pattern at
    # any depth. A directory entry prunes the whole subtree.
    out=""
    for p in $paths; do
      out="$out */$source_root/$p"
    done
    printf "%s\\n" "${out# }"
    ;;

  --filter)
    # A path is excluded when it equals an entry or sits under one. Prefix
    # matching on real paths - no globs, nothing to get subtly wrong.
    # `|| [ -n "$f" ]` so a final line with no trailing newline is still read -
    # git and find both emit one, but a caller piping a plain string does not,
    # and silently dropping the last file is a horrible way to find that out.
    while IFS= read -r f || [ -n "$f" ]; do
      [ -n "$f" ] || continue
      if [ -n "$source_root" ]; then
        case "$f" in
          "$source_root"/*) rel=${f#"$source_root"/} ;;
          *) continue ;;   # outside the source root: not ours to format
        esac
      else
        rel=$f
      fi
      excluded=0
      for p in $paths; do
        case "$rel" in
          "$p"|"$p"/*) excluded=1; break ;;
        esac
      done
      [ "$excluded" -eq 0 ] && printf "%s\\n" "$f"
    done
    ;;

  --list)
    find "$root/${source_root:-.}" -name "*.c" -o -name "*.h" \
      | sed "s|^$root/||" | sort | "$0" --filter
    ;;

  *)
    echo "usage: $0 [--source-root|--globs|--filter|--list]" >&2
    exit 2
    ;;
esac
