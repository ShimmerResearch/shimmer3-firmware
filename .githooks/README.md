# Checked-in git hooks

```
.githooks/install.sh      # Linux / macOS
.githooks\install.bat     # Windows
```

Run once per clone. It sets `core.hooksPath` to this directory, for this
repository and — from a firmware checkout — for the `log-and-stream-common`
submodule as well, because commits made inside the submodule are its commits and
need their own configuration.

## What the hook does

`pre-commit` runs `clang-format` over the `.c`/`.h` files **staged for this
commit** and re-stages them. Only staged files, so it is sub-second whatever the
repository size.

The point is the round-trip. `clang-format-check.yml` already reformats pushed
code and commits the result back on your branch — which works, but means your
branch moves under you, you pull before the next push, and the diff that was
reviewed is not the diff that shipped. Roughly 15% of commits in
`log-and-stream-common` are those bot commits. Format at commit time and they
never appear.

**CI stays as it is.** The hook removes the round-trip for anyone who runs the
installer; the workflow still catches fresh clones, people who skipped setup, and
edits made through the GitHub web UI.

## What it will not do

- **Block a commit.** No clang-format on the machine, or a file only partly
  staged, and it says so and lets the commit through.
- **Touch a partly staged file.** After `git add -p`, formatting would rewrite
  the whole file and re-staging would quietly commit the part you held back. It
  skips those and tells you. Stage the whole file if you want it formatted.
- **Format anything CI does not.** The `SOURCE_ROOT` and `EXCLUDE_RE` at the top
  of the hook mirror `.github/workflows/clang-format-check.yml`. **If the
  workflow's lists change, change the hook's in the same commit** — otherwise
  local and CI format different sets of files and fight over the difference.

## Which clang-format it uses

The binary bundled at `Extras/clang-format-all-win64/clang-format.exe` is
preferred, so every developer uses the same one. On Linux and macOS that is a
Windows build and not runnable, so `clang-format` from `PATH` is used instead —
install it from your package manager there.

Nothing needs installing on Windows: Git for Windows supplies the shell that runs
the hook, and the formatter is already in the clone.

## Bypassing

```
git commit --no-verify
```

Hooks are advisory, not enforcement — that is what CI is for.
