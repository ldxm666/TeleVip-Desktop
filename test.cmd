@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_VER (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%I\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 exit /b 1
cd src
if not exist build\tests mkdir build\tests
for %%T in (test_translate_click_guard test_translate_bar_recognition test_json_text test_ui_layout test_motion test_license_lifecycle test_archive_loader test_news) do (
 cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I. host_tests\%%T.cpp /Fo:build\tests\%%T.obj /Fe:build\tests\%%T.exe
 if errorlevel 1 exit /b 1
 build\tests\%%T.exe
 if errorlevel 1 exit /b 1
)
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /I. common.cpp host_tests\test_translation_request.cpp /Fo:build\tests\ /Fe:build\tests\test_translation_request.exe
if errorlevel 1 exit /b 1
build\tests\test_translation_request.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /I. common.cpp preview_platform.cpp render.cpp host_tests\test_license_crypto.cpp /Fo:build\tests\ /Fe:build\tests\test_license_crypto.exe
if errorlevel 1 exit /b 1
build\tests\test_license_crypto.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I. common.cpp preview_platform.cpp native_translate.cpp host_tests\test_translation_server.cpp /Fo:build\tests\ /Fe:build\tests\test_translation_server.exe
if errorlevel 1 exit /b 1
build\tests\test_translation_server.exe
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /I. common.cpp telegram_link.cpp host_tests\test_telegram_link.cpp /Fo:build\tests\ /Fe:build\tests\test_telegram_link.exe
if errorlevel 1 exit /b 1
build\tests\test_telegram_link.exe
if errorlevel 1 exit /b 1
exit /b %errorlevel%
