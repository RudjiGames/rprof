@echo off
echo This will clone dependencies in the parent directory.
echo Note that this is only needed if you want to generate the demo project.
echo _
:choice
set /P c=Are you sure you want to continue[Y/N]?
if /I "%c%" EQU "Y" goto :clone_deps
if /I "%c%" EQU "N" goto :exit
goto :choice

:clone_deps
if not exist ../../bx   ( git clone https://github.com/bkaradzic/bx.git   ../../bx   ) else ( echo skipping bx     - directory exists )
if not exist ../../bimg ( git clone https://github.com/bkaradzic/bimg.git ../../bimg ) else ( echo skipping bimg   - directory exists )
if not exist ../../bgfx ( git clone https://github.com/bkaradzic/bgfx.git ../../bgfx ) else ( echo skipping bgfx   - directory exists )

if not exist ../../build  ( git clone https://github.com/RudjiGames/build.git ../../build  )  else ( echo skipping build  - directory exists )
if not exist ../../rbase  ( git clone https://github.com/RudjiGames/rbase.git ../../rbase   ) else ( echo skipping rbase  - directory exists )
if not exist ../../rapp   ( git clone https://github.com/RudjiGames/rapp.git  ../../rapp  )   else ( echo skipping rapp   - directory exists )

:exit
