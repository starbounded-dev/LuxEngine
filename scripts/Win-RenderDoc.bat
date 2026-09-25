@echo off
rem Runs the editor inside RenderDoc: RenderDoc opens with the editor already launched and hooked.
rem
rem   scripts\Win-RenderDoc.bat              prompts for a configuration
rem   scripts\Win-RenderDoc.bat debug        non-interactive (debug / release / dist)
rem   scripts\Win-RenderDoc.bat debug -foo   extra arguments are forwarded to Editor.exe
rem
rem Build the editor in Visual Studio first; this script does not build. Capture with F12 or
rem Print Screen in the editor, or Trigger Capture in RenderDoc's Launch tab. RenderDoc is found
rem through RENDERDOC_DIR, then PATH, then its default install folder. It is a developer tool,
rem not something games need.

setlocal
set "LUX_DIR=%~dp0.."
for %%I in ("%LUX_DIR%") do set "LUX_DIR=%%~fI"

set "CONFIG=%~1"
if "%CONFIG%"=="" (
	echo Select build configuration:
	echo   1^) Debug
	echo   2^) Release
	echo   3^) Dist
	set /p "CHOICE=Choice [1-3]: "
) else (
	shift
)
if "%CONFIG%"=="" (
	if "%CHOICE%"=="1" set "CONFIG=Debug"
	if "%CHOICE%"=="2" set "CONFIG=Release"
	if "%CHOICE%"=="3" set "CONFIG=Dist"
)
if /i "%CONFIG%"=="debug" set "CONFIG=Debug"
if /i "%CONFIG%"=="release" set "CONFIG=Release"
if /i "%CONFIG%"=="dist" set "CONFIG=Dist"
rem Messages that print paths stay outside parenthesised blocks: cmd expands a block's variables
rem before parsing it, so a path such as "Program Files (x86)" would break the block.
if "%CONFIG%"=="Debug" goto config_ok
if "%CONFIG%"=="Release" goto config_ok
if "%CONFIG%"=="Dist" goto config_ok
echo Unknown configuration "%CONFIG%". Usage: %~nx0 [debug^|release^|dist] [editor arguments]
exit /b 1
:config_ok

set "EDITOR_EXE=%LUX_DIR%\bin\%CONFIG%-windows-x86_64\Editor\Editor.exe"
if exist "%EDITOR_EXE%" goto editor_ok
echo %EDITOR_EXE% not found. Build the %CONFIG% configuration in Visual Studio first.
exit /b 1
:editor_ok

set "QRENDERDOC="
if defined RENDERDOC_DIR if exist "%RENDERDOC_DIR%\qrenderdoc.exe" set "QRENDERDOC=%RENDERDOC_DIR%\qrenderdoc.exe"
if not defined QRENDERDOC for %%Q in (qrenderdoc.exe) do if not "%%~$PATH:Q"=="" set "QRENDERDOC=%%~$PATH:Q"
if not defined QRENDERDOC if exist "%ProgramFiles%\RenderDoc\qrenderdoc.exe" set "QRENDERDOC=%ProgramFiles%\RenderDoc\qrenderdoc.exe"
if defined QRENDERDOC goto renderdoc_ok
echo qrenderdoc.exe not found. Install RenderDoc from https://renderdoc.org, add it to PATH,
echo or set RENDERDOC_DIR to its install folder.
exit /b 1
:renderdoc_ok

rem Remaining arguments go to the editor. Quotes are dropped; JSON needs backslashes doubled.
set "ARGS="
:collect_args
if "%~1"=="" goto args_done
set "ARGS=%ARGS% %~1"
shift
goto collect_args
:args_done
if defined ARGS set "ARGS=%ARGS:~1%"
if defined ARGS set "ARGS=%ARGS:\=\\%"

set "JSON_EXE=%EDITOR_EXE:\=\\%"
set "JSON_DIR=%LUX_DIR%\Editor"
set "JSON_DIR=%JSON_DIR:\=\\%"

set "CAPTURE_DIR=%TEMP%\luxengine"
if not exist "%CAPTURE_DIR%" mkdir "%CAPTURE_DIR%"
set "SETTINGS=%CAPTURE_DIR%\Editor-%CONFIG%.cap"

> "%SETTINGS%" echo {
>>"%SETTINGS%" echo     "rdocCaptureSettings": 1,
>>"%SETTINGS%" echo     "settings": {
>>"%SETTINGS%" echo         "autoStart": true,
>>"%SETTINGS%" echo         "commandLine": "%ARGS%",
>>"%SETTINGS%" echo         "environment": [],
>>"%SETTINGS%" echo         "executable": "%JSON_EXE%",
>>"%SETTINGS%" echo         "inject": false,
>>"%SETTINGS%" echo         "numQueuedFrames": 0,
>>"%SETTINGS%" echo         "options": {
>>"%SETTINGS%" echo             "allowFullscreen": true,
>>"%SETTINGS%" echo             "allowVSync": true,
>>"%SETTINGS%" echo             "apiValidation": false,
>>"%SETTINGS%" echo             "captureAllCmdLists": false,
>>"%SETTINGS%" echo             "captureCallstacks": false,
>>"%SETTINGS%" echo             "captureCallstacksOnlyDraws": false,
>>"%SETTINGS%" echo             "debugOutputMute": true,
>>"%SETTINGS%" echo             "delayForDebugger": 0,
>>"%SETTINGS%" echo             "hookIntoChildren": false,
>>"%SETTINGS%" echo             "refAllResources": false,
>>"%SETTINGS%" echo             "softMemoryLimit": 0,
>>"%SETTINGS%" echo             "verifyBufferAccess": false
>>"%SETTINGS%" echo         },
>>"%SETTINGS%" echo         "queuedFrameCap": 0,
>>"%SETTINGS%" echo         "workingDir": "%JSON_DIR%"
>>"%SETTINGS%" echo     }
>>"%SETTINGS%" echo }

echo Launching %CONFIG% editor in RenderDoc (%SETTINGS%).
echo Capture with F12 or Print Screen in the editor, or Trigger Capture in RenderDoc's Launch tab.
start "" "%QRENDERDOC%" "%SETTINGS%"
endlocal
