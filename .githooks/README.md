# Checked-in git hooks

```
.githooks/install.sh      # Linux / macOS
.githooks\install.bat     # Windows
```

Run once per clone. It sets `core.hooksPath` to this directory for the
repository, and for every checked-out submodule that has a `.githooks` of its
own — `log-and-stream-common`, in a Shimmer3 or Shimmer3R checkout — because
commits made inside a submodule are its commits and need their own configuration.

Worktrees share their clone's setting, so they need nothing — except that a
worktree's submodules are separate checkouts with their own configuration. If
you initialise submodules in a worktree and commit inside them, run the installer
there too.

## One copy, four repositories

The hook, both installers, this README, `scripts/clang-format-exclude.sh` and
`.github/workflows/clang-format-check.yml` are identical, byte for byte, in
`verisense-firmware`, `shimmer3-firmware`, `shimmer3r-firmware` and
`log-and-stream-common`. Change one and copy it to the other three.

What differs between repositories is data: `.clang-format-exclude` says what is
formatted and with which clang-format, and `.clang-format` says how.

## What the hook does

`pre-commit` runs `clang-format` over the `.c`/`.h` files **staged for this
commit** and re-stages them. Only staged files, so it is sub-second whatever the
repository size.

The point is the round-trip. `clang-format-check.yml` already reformats pushed
code and commits the result back on your branch — which works, but means your
branch moves under you, the next commit made without pulling conflicts with it,
and the diff that was reviewed is not the diff that shipped. In 2026, 13–19% of
the commits in each of these repositories were those bot commits. Format at
commit time and they never appear.

**CI stays as it is.** The hook removes the round-trip for anyone who runs the
installer; the workflow still catches fresh clones, people who skipped setup, and
edits made through the GitHub web UI.

## What it will not do

- **Block a commit.** No clang-format on the machine, the wrong version, a file
  only partly staged, or a `.clang-format-exclude` it cannot read: it says so and
  lets the commit through.
- **Touch a partly staged file.** After `git add -p`, formatting would rewrite
  the whole file and re-staging would quietly commit the part you held back. It
  skips those and tells you. Stage the whole file if you want it formatted.
- **Format with a different clang-format from CI's.** Two versions can disagree
  about a line, and then the hook and CI undo each other's work on every push —
  the bot commits this exists to remove, back again. The version CI runs is
  pinned in `.clang-format-exclude`; if the binary the hook finds is any other,
  it skips and says which it found.
- **Format anything CI does not.** Both read the source roots and exclusions from
  `.clang-format-exclude` through `scripts/clang-format-exclude.sh`, so they
  cover exactly the same files. Add a vendor directory there and both follow.

## `.clang-format-exclude`

At the repository root: the clang-format version, the source roots, and the
paths under them that clang-format must not touch. It is the **only** place any
of that lives. The workflow reads it through `scripts/clang-format-exclude.sh
--sources`, `--globs` and `--clang-format-version`; the hook through `--filter`
and `--clang-format-version`. In the Shimmer3 repositories the whole-project
formatter, `Extras/clang-format-all-win64/*.bat`, reads it directly.

`scripts/clang-format-exclude.sh --list` prints every file that is formatted.
The format itself is described at the top of the file.

## Which clang-format it uses

The binary bundled at `Extras/clang-format-all-win64/clang-format.exe` is
preferred, so every developer uses the same one. On Linux and macOS that is a
Windows build and not runnable, so `clang-format` from `PATH` is used instead —
install the pinned version from your package manager or LLVM's releases there.

Nothing needs installing on Windows: Git for Windows supplies the shell that runs
the hook, and the formatter is already in the clone.

**Moving to another clang-format** is one change, made in all four repositories
together: the `clang-format-version:` line, the bundled `clang-format.exe`, and
whatever the new version reformats. Until the bundled binary matches the new pin
the hook skips rather than fight CI, so a half-done bump costs round-trips, not
churn.

## Bypassing

```
git commit --no-verify
```

Hooks are advisory, not enforcement — that is what CI is for.
