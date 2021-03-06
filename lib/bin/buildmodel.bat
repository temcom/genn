@echo off

echo warning: buildmodel.bat has been depreciated!
echo please use the new genn-buildmodel.bat script in future

set MODELPATH=%cd%
echo model path: %MODELPATH%
set MODELNAME=%1
echo model name: %MODELNAME%
set DBGMODE=0
set CPU_ONLY=0
set k=0
setlocal ENABLEDELAYEDEXPANSION
for %%x in (%*) do (
    set /a k+=1
    set "argv[!k!]=%%~x"
)
for /l %%i in (1,1,%k%) do (
    if "!argv[%%i]!"=="DEBUG" (
      set /a j=%%i+1
      set /a DBGMODE="argv[!j!]"
    )  
    if "!argv[%%i]!"=="CPU_ONLY" (
      set /a j=%%i+1
      set /a CPU_ONLY="argv[!j!]"
    )
) 
for /f %%a in ('set DBGMODE^&set CPU_ONLY') do (if "!"=="" endlocal)&set "%%a"
echo debug mode: %DBGMODE%
echo cpu only: %CPU_ONLY%

if "%GENN_PATH%"=="" (
  if "%GeNNPATH%"=="" (
    echo ERROR: Environment variable 'GENN_PATH' has not been defined. Quitting...
    exit
  )
  echo Environment variable 'GeNNPATH' will be replaced by 'GENN_PATH' in future GeNN releases.
  set GENN_PATH=%GeNNPATH%
)

nmake clean /nologo /f "%GENN_PATH%\lib\WINmakefile"
if "%DBGMODE%"=="0" (
  nmake /nologo /f "%GENN_PATH%\lib\WINmakefile" MODEL="%MODELPATH%\%MODELNAME%.cc" CPU_ONLY=%CPU_ONLY%
  .\generateALL.exe %MODELPATH%
) else (
  nmake /nologo /f "%GENN_PATH%\lib\WINmakefile" DEBUG=1 MODEL="%MODELPATH%\%MODELNAME%.cc" EXTRA_DEF=%EXTRA_DEF%
  devenv /debugexe .\generateALL.exe %MODELPATH%
)

echo Model build complete ...
