@echo off
REM Point git at the hooks checked in under .githooks\. Run once per clone.
REM Undo with:  git config --unset core.hooksPath
setlocal
cd /d "%~dp0.."

git config core.hooksPath .githooks
if errorlevel 1 (
  echo Failed to configure git hooks - is git on PATH?
  endlocal
  exit /b 1
)
echo Hooks enabled: %CD%

REM A submodule is a separate repository: commits made inside it are its
REM commits, and need its own hook configuration. Configure every checked-out
REM submodule that carries a .githooks of its own - log-and-stream-common, in a
REM Shimmer3 or Shimmer3R checkout. Vendor submodules have none and are left
REM alone. git runs the quoted command in its own sh, once per submodule.
git submodule --quiet foreach --recursive "if [ -d .githooks ]; then git config core.hooksPath .githooks && echo Hooks enabled: $toplevel/$sm_path; fi"
if errorlevel 1 (
  echo Failed to configure the submodules' hooks.
  endlocal
  exit /b 1
)

echo.
echo Staged .c/.h files are now clang-formatted as part of each commit.
echo Bypass a single commit with: git commit --no-verify
endlocal
exit /b 0
