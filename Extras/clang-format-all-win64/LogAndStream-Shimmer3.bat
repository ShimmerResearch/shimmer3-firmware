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
REM ones, so the body only ever sees the version, the source-root line and the
REM exclusions. The version needs nothing doing - clang-format.exe beside this
REM file is that version - so it is only reported.
REM No pipes: escaping one inside a for /f is a well-known way to get this wrong.
set "SRC="
set "VER="
set "ROOTS=0"
set "ARGS="
for /f "usebackq eol=# tokens=1,* delims=:" %%A in ("%CFG%") do (
  if /i "%%A"=="source-root" (
    set "SRC=%%B"
    set /a ROOTS+=1
  ) else if /i "%%A"=="clang-format-version" (
    set "VER=%%B"
  ) else (
    set "ARGS=!ARGS! -I %%A"
  )
)

REM clang-format-all takes one source directory, and every -I applies to it.
if !ROOTS! GTR 1 (
  echo ERROR: %CFG% names !ROOTS! source roots; this formats exactly one.
  exit /b 1
)

REM %%B keeps the space after the colon; paths never contain one. "source-root:"
REM with nothing after it leaves SRC undefined, and substituting in an undefined
REM variable does not give an empty string, hence the guards.
if defined SRC set "SRC=!SRC: =!"
if defined VER set "VER=!VER: =!"

if "!SRC!"=="" (set "TARGET=../../") else (set "TARGET=../../!SRC!")

echo Formatting !TARGET! with clang-format !VER!
.\clang-format-all-win64.exe -S !TARGET! -E .\clang-format.exe!ARGS!
set "RC=!errorlevel!"

endlocal & exit /b %RC%
