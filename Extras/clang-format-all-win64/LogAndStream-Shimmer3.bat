@echo off
REM https://github.com/GloryOfNight/clang-format-all
REM
REM Formats the whole project in one go.
REM
REM The source root and the exclusion list come from .clang-format-exclude at the
REM repository root - the same file CI and .githooks\pre-commit read. Edit the
REM list there, never here.

setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CFG=..\..\.clang-format-exclude"
if not exist "%CFG%" (
  echo ERROR: cannot find %CFG%
  exit /b 1
)

REM One pass over the file. "eol=#" skips comment lines and for /f skips blank
REM ones, so the body only ever sees the source-root line and the exclusions.
REM No pipes: escaping one inside a for /f is a well-known way to get this wrong.
set "SRC="
set "ARGS="
for /f "usebackq eol=# tokens=1,* delims=:" %%A in ("%CFG%") do (
  if /i "%%A"=="source-root" (
    set "SRC=%%B"
  ) else (
    set "ARGS=!ARGS! -I %%A"
  )
)
REM %%B keeps the space after the colon; paths never contain one.
set "SRC=!SRC: =!"

if "!SRC!"=="" (set "TARGET=../../") else (set "TARGET=../../!SRC!")

echo Formatting !TARGET!
.\clang-format-all-win64.exe -S !TARGET! -E .\clang-format.exe!ARGS!
set "RC=!errorlevel!"

endlocal & exit /b %RC%
