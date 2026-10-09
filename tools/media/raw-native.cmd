@echo off
rem raw-native: the command-line front for raw-native tools. Today: media.
if "%1"=="media" (
  shift
  python "%~dp0raw_native_media.py" %2 %3 %4 %5 %6 %7 %8 %9
  exit /b %errorlevel%
)
echo usage: raw-native media render^|facts^|attach ...
exit /b 2
