@echo off
REM Point git at the hooks checked in under .githooks\. Run once per clone.
REM Undo with:  git config --unset core.hooksPath
setlocal enabledelayedexpansion
cd /d "%~dp0.."

git config core.hooksPath .githooks
if errorlevel 1 (
  echo Failed to configure git hooks - is git on PATH?
  endlocal
  exit /b 1
)
echo Hooks enabled: %CD%

REM The shared library is a separate repository. Editing it from inside a
REM firmware checkout makes commits in ITS repo, not the parent's, so it needs
REM its own hook configuration - one clone, two places to set this.
for /d %%S in (LogAndStream_Shimmer3*) do (
  if exist "%%S\log-and-stream-common\.githooks" (
    git -C "%%S\log-and-stream-common" config core.hooksPath .githooks
    echo Hooks enabled: %%S\log-and-stream-common
  )
)

echo.
echo Staged .c/.h files are now clang-formatted as part of each commit.
echo Bypass a single commit with: git commit --no-verify
endlocal
exit /b 0
