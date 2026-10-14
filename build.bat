@echo off
echo Setting up Visual Studio environment...

REM Try different VS installation paths
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo Visual Studio not found in standard locations
    echo Please manually set up the build environment
    pause
    exit /b 1
)

echo Building project...
msbuild external-cheat-base.sln /p:Configuration=Release /p:Platform=x64

if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
) else (
    echo Build successful!
    pause
)