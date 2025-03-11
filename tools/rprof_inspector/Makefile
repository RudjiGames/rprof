
CXX = emcc
OUTPUT = rprof.js

SOURCES = main.cpp
SOURCES += ../../3rd/imgui/imgui.cpp
SOURCES += ../../3rd/imgui/imgui_draw.cpp
SOURCES += ../../3rd/imgui/imgui_tables.cpp
SOURCES += ../../3rd/imgui/imgui_widgets.cpp 
SOURCES += ../../3rd/imgui/backends/imgui_impl_glfw.cpp
SOURCES += ../../3rd/imgui/backends/imgui_impl_opengl3.cpp
SOURCES += ../../3rd/implot/implot.cpp
SOURCES += ../../3rd/implot/implot_items.cpp
SOURCES += ../../src/rprof_context.cpp 
SOURCES += ../../src/rprof_freelist.cpp 
SOURCES += ../../src/rprof_lib.cpp 

INCLUDES = -I../../3rd/imgui -I../../3rd/implot
#LIBS = -lGL
LIBS = -lGL-webgl2-full_es3-getprocaddr
WEBGL_VER = -s USE_WEBGL2=1 -s USE_GLFW=3 -s FULL_ES3=1
FILE_SYSTEM = -s FORCE_FILESYSTEM=1 -s ASYNCIFY=1 --memory-init-file 0 -DIMGUI_DISABLE_DEBUG_TOOLS
USE_WASM = -s WASM=1

PROJECTS :=  gui

GUI: 
	$(CXX)  $(SOURCES) -o $(OUTPUT) $(LIBS) $(WEBGL_VER) $(FILE_SYSTEM) $(INCLUDES) -O2 --no-heap-copy -s ALLOW_MEMORY_GROWTH=1 --preload-file data $(USE_WASM) -sASSERTIONS 

all: $(GUI)

clean:
	rm -f $(OUTPUT)
