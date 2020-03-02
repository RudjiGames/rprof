
CXX = emcc
OUTPUT = rprof.js

SOURCES = main.cpp
SOURCES += ../rprof_inspector/imgui/imgui.cpp
SOURCES += ../rprof_inspector/imgui/imgui_draw.cpp 
SOURCES += ../rprof_inspector/imgui/imgui_impl_glfw.cpp
SOURCES += ../rprof_inspector/imgui/imgui_impl_opengl3.cpp
SOURCES += ../../src/rprof_context.cpp 
SOURCES += ../../src/rprof_freelist.cpp 
SOURCES += ../../src/rprof_lib.cpp 

LIBS = -lGL
WEBGL_VER = -s USE_WEBGL2=1 -s USE_GLFW=3 -s FULL_ES3=1
FILE_SYSTEM = -s FORCE_FILESYSTEM=1 -s ASYNCIFY=1 --memory-init-file 0
USE_WASM = -s WASM=1

PROJECTS :=  gui

GUI: 
	$(CXX)  $(SOURCES) -o $(OUTPUT) $(LIBS) $(WEBGL_VER) $(FILE_SYSTEM) -O2 --preload-file data $(USE_WASM)

all: $(GUI)

clean:
	rm -f $(OUTPUT)
