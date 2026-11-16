@echo off
rem Builds the client, server and UI and packs them like the "Playable Skyrim Together Build" workflow does.
rem Run it from the "x64 Native Tools Command Prompt for VS 2022":
rem
rem   Tools\Scripts\package_playable.bat          build in release mode (the default)
rem   Tools\Scripts\package_playable.bat ui       also rebuild the UI, it is only built when missing otherwise
rem
rem The result is build\SkyrimTogether-custom.zip, with the same layout as the official mod archive.

setlocal
cd /d "%~dp0..\.." || exit /b 1

set "OUT=build\windows\x64\release"
set "PACKAGE=build\package"
set "ZIP=build\SkyrimTogether-custom.zip"

echo === Building ===
call xmake f -m release -y || exit /b 1
call xmake -y || exit /b 1
call xmake install -o distrib || exit /b 1
xcopy /E /Y /Q distrib\bin\* "%OUT%\" >nul || exit /b 1

if not exist "%OUT%\config\STServer.ini" (
    "%OUT%\SkyrimTogetherServer.exe" --generate-config "%OUT%\config\STServer.ini" || exit /b 1
)

set "BUILD_UI="
if /i "%~1"=="ui" set "BUILD_UI=1"
if not exist Code\skyrim_ui\dist\UI set "BUILD_UI=1"
if defined BUILD_UI (
    echo === Building the UI ===
    pushd Code\skyrim_ui || exit /b 1
    call pnpm install || exit /b 1
    call pnpm deploy:production || exit /b 1
    popd
)
xcopy /E /I /Y /Q Code\skyrim_ui\dist\UI "%OUT%\UI" >nul || exit /b 1

echo === Packaging ===
if exist "%PACKAGE%" rmdir /S /Q "%PACKAGE%"
if exist "%ZIP%" del "%ZIP%"
mkdir "%PACKAGE%\SkyrimTogetherReborn" || exit /b 1
xcopy /E /Y /Q "%OUT%\*" "%PACKAGE%\SkyrimTogetherReborn\" >nul || exit /b 1
xcopy /E /Y /Q GameFiles\Skyrim\* "%PACKAGE%\" >nul || exit /b 1

rem Logs and crash dumps of local test runs don't belong in the archive
if exist "%PACKAGE%\SkyrimTogetherReborn\logs" rmdir /S /Q "%PACKAGE%\SkyrimTogetherReborn\logs"
del /Q "%PACKAGE%\SkyrimTogetherReborn\*.dmp" 2>nul

powershell -NoProfile -Command "Compress-Archive -Path '%PACKAGE%\*' -DestinationPath '%ZIP%' -Force" || exit /b 1

echo.
echo Done: %CD%\%ZIP%
endlocal
