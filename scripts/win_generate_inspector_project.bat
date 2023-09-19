@echo off
IF not [%1]==[] goto EMSDK_PATH_ARG
echo Example usage: get_inspector_proj.bat D:/emsdk
GOTO END
:EMSDK_PATH_ARG
set EMSCRIPTEN=%1/upstream/emscripten
call %1/emsdk_env.bat
cd ../genie
genie --gcc=asmjs gmake
cd ../scripts
:END
