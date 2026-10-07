@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VCVARS%" (
  echo MSVC 2022 Build Tools not found. Install Visual Studio 2022 Build Tools with the C++ workload.
  pause
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
  echo Failed to run vcvars64.bat
  pause
  exit /b 1
)

set "CMAKE_EXE="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
  set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
  set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not defined CMAKE_EXE (
  where cmake >nul 2>&1
  if errorlevel 1 (
    echo cmake not found. Install the CMake component of Visual Studio 2022 Build Tools.
    pause
    exit /b 1
  )
  set "CMAKE_EXE=cmake"
)

if not defined VCPKG_ROOT (
  if exist "%~dp0vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set "VCPKG_ROOT=%~dp0vcpkg"
  )
)
if not defined VCPKG_ROOT (
  echo VCPKG_ROOT is not set and no repo-local vcpkg\ was found.
  echo Clone vcpkg and bootstrap it, then either:
  echo   set VCPKG_ROOT=C:\path\to\vcpkg
  echo or place it at "%~dp0vcpkg"
  echo.
  echo Example:
  echo   git clone https://github.com/microsoft/vcpkg "%~dp0vcpkg"
  echo   "%~dp0vcpkg\bootstrap-vcpkg.bat"
  pause
  exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo VCPKG_ROOT=%VCPKG_ROOT% does not contain scripts\buildsystems\vcpkg.cmake
  pause
  exit /b 1
)

echo Configuring preset windows-msvc ^(VCPKG_ROOT=%VCPKG_ROOT%^)...
"%CMAKE_EXE%" --preset windows-msvc
if errorlevel 1 (
  echo.
  echo CMake configure failed.
  pause
  exit /b 1
)

echo Building preset windows-msvc...
"%CMAKE_EXE%" --build --preset windows-msvc
if errorlevel 1 (
  echo.
  echo Build failed.
  pause
  exit /b 1
)

echo.
echo Built build\bin\raytracer.exe
pause
exit /b 0
