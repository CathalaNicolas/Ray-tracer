@echo off
cd /d "%~dp0"
if not exist dist mkdir dist
mingw32-make -j1
if errorlevel 1 (
  echo.
  echo Build failed.
  pause
  exit /b 1
)
echo.
echo Built raytracer.exe
pause
exit /b 0
