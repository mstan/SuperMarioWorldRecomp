@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c11 /D_CRT_SECURE_NO_WARNINGS /W4 /O2 /I..\.. /I..\..\src /I..\..\snesrecomp\runner\src /I..\..\recomp /Fe:falcon_kick_guard_test.exe falcon_kick_guard_test.c ..\..\overrides\falcon\falcon_smw_adapter.c ..\..\src\mods\falcon\falcon_locomotion.c ..\..\src\mods\falcon\captain_falcon_foreign.c ..\..\src\mods\falcon\smw_falcon_combat_policy.c ..\..\src\mods\falcon\smw_falcon_combat_apply.c ..\..\src\foreign_controller.c
if errorlevel 1 exit /b %errorlevel%
falcon_kick_guard_test.exe
