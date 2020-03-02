@echo off
IF not [%1]==[] goto EMSDK_PATH_ARG
echo Example usage: get_inspectro_proj.bat D:/emsdk
GOTO END
:EMSDK_PATH_ARG
set EMSCRIPTEN=%1/upstream/emscripten
call %1/emsdk_env.bat
genie --gcc=asmjs gmake
:END
