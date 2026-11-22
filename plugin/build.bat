@echo off
rem ---------------------------------------------------------------------------
rem  Extended Cut plugin - command-line build (Release, x86)
rem  Same compiler / linker flags as the Visual Studio project
rem  (the original Visual Studio project, Release|Win32).
rem
rem  Output:  out\ExtendedCut.asi, out\ExtendedCut.pdb, out\SDL3.dll
rem  Temp:    out\build\  (object files etc., safe to delete)
rem
rem  SDL3 location can be overridden:  set SDL3_DIR=C:\path\to\SDL3-3.4.18
rem ---------------------------------------------------------------------------
setlocal

set "ROOT=%~dp0.."
set "SRC=%~dp0ExtendedCut.cpp"
set "OUT=%ROOT%\out"
set "OBJ=%OUT%\build"
set "DEFS="
rem  "build.bat release" = public release build in out\release_plugin\ (achievements off + tab hidden)
if /i "%~1"=="release" (
  set "OUT=%ROOT%\out\release_plugin"
  set "OBJ=%ROOT%\out\release_plugin\build"
  set "DEFS=/D EC_RELEASE"
)
if not defined SDL3_DIR set "SDL3_DIR=%~dp0SDL3-3.4.18"

if not exist "%SDL3_DIR%\include\SDL3\SDL.h" (
  echo ERROR: SDL3 not found in "%SDL3_DIR%"
  exit /b 1
)

rem --- find Visual Studio and load the x86 compiler --------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo ERROR: Visual Studio with C++ tools not found
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>nul || exit /b 1

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

rem --- compile ----------------------------------------------------------------
cl /c /nologo /I"%SDL3_DIR%\include" /Zi /W3 /WX- /diagnostics:column /sdl /O2 /Oi /Oy- /GL ^
   /D WIN32 /D NDEBUG /D PUNISHERPAD_EXPORTS /D _WINDOWS /D _USRDLL /D _WINDLL /D _UNICODE /D UNICODE %DEFS% ^
   /Gm- /EHsc /MD /GS /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /std:c++20 /permissive- ^
   /external:W3 /Gd /TP /FC /Fo"%OBJ%\ExtendedCut.obj" /Fd"%OBJ%\vc.pdb" "%SRC%" || goto :fail

rem --- link -------------------------------------------------------------------
link /NOLOGO /DLL /OUT:"%OUT%\ExtendedCut.asi" /LIBPATH:"%SDL3_DIR%\lib\x86" ^
   SDL3.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ^
   ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ^
   /MANIFEST /MANIFESTUAC:NO /manifest:embed /manifestinput:"%VCToolsInstallDir%include\manifest\segmentheap.manifest" ^
   /DEBUG /PDB:"%OUT%\ExtendedCut.pdb" /PDBALTPATH:ExtendedCut.pdb /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /LTCG ^
   /TLBID:1 /DYNAMICBASE /NXCOMPAT /IMPLIB:"%OBJ%\ExtendedCut.lib" /MACHINE:X86 /SAFESEH ^
   "%OBJ%\ExtendedCut.obj" || goto :fail

copy /y "%SDL3_DIR%\lib\x86\SDL3.dll" "%OUT%\SDL3.dll" >nul || goto :fail

echo.
echo BUILD OK: %OUT%\ExtendedCut.asi
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1
