@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_VER (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%I\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 (echo Open an x64 Native Tools Command Prompt for Visual Studio. & exit /b 1)
if not exist build mkdir build
cd src
if not exist build mkdir build
rc /nologo /fo build\app.res res\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I. ^
 /I"%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt" ^
 common.cpp i18n.cpp preview_platform.cpp tools.cpp render.cpp overlay.cpp native_translate.cpp app.cpp support.cpp public_main.cpp ^
 /Fo:build\ /Fe:..\build\TeleVipNative-Preview.exe ^
 /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF windowsapp.lib gdiplus.lib build\app.res
if errorlevel 1 exit /b 1
echo Built: build\TeleVipNative-Preview.exe -- public UI preview, private Hook unavailable.
