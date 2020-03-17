#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <algorithm>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define GLFW_INCLUDE_ES3
#include <GLES3/gl3.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "../../3rd/lz4-r191/lz4.h"

#include "../../inc/rprof.h"
#include "../../inc/rprof_imgui.h"

GLFWwindow*		g_window;
ImGuiContext*	imgui = 0;

EM_JS(int,	canvas_get_width, (),	{ return Module.canvas.width; });
EM_JS(int,	canvas_get_height, (),	{ return Module.canvas.height; });
EM_JS(void,	resizeCanvas, (),		{ js_resizeCanvas(); });

int init()
{
	if( !glfwInit() )
	{
		fprintf( stderr, "Failed to initialize GLFW\n" );
		return 1;
	}

	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // We don't want the old OpenGL
																   // Open a window and create its OpenGL context
	int canvasWidth = 800;
	int canvasHeight = 600;
	g_window = glfwCreateWindow( canvasWidth, canvasHeight, "Spike profiler", NULL, NULL);
	if( g_window == NULL )
	{
		fprintf( stderr, "Failed to open GLFW window.\n" );
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(g_window); // Initialize GLEW

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplGlfw_InitForOpenGL(g_window, true);
	ImGui_ImplOpenGL3_Init();

	ImGui::StyleColorsDark();

	io.Fonts->AddFontFromFileTTF("data/MavenPro-Regular.ttf", 18.0f);
	io.Fonts->AddFontFromFileTTF("data/MavenPro-Regular.ttf", 15.0f);
	io.Fonts->AddFontFromFileTTF("data/MavenPro-Regular.ttf", 23.0f);
	io.Fonts->AddFontFromFileTTF("data/MavenPro-Regular.ttf", 29.0f);
	io.Fonts->AddFontDefault();

	imgui = ImGui::GetCurrentContext();

	glfwSetMouseButtonCallback(g_window, ImGui_ImplGlfw_MouseButtonCallback);
	glfwSetScrollCallback(g_window, ImGui_ImplGlfw_ScrollCallback);
	glfwSetKeyCallback(g_window, ImGui_ImplGlfw_KeyCallback);
	glfwSetCharCallback(g_window, ImGui_ImplGlfw_CharCallback);

	resizeCanvas();

	return 0;
}

ProfilerFrame	g_frame;

void quit()
{
	rprofRelease(&g_frame);
	glfwTerminate();
}

void profilerFrameLoadCallback(const char* _name)
{
	FILE* file = fopen(_name, "rb");
	if (file)
	{
		fseek(file, 0, SEEK_END);
		long csize = ftell(file);
		fseek(file, 0, SEEK_SET);

		static const size_t maxDecompSize = 4 * 1024 * 1024;
		char* compBuffer	= (char*)malloc(csize);
		char* decompBuffer	= (char*)malloc(maxDecompSize);
		char* bufferReadPtr	= decompBuffer;

		fread(compBuffer, 1, csize, file);
		rprofLoad(&g_frame, compBuffer, csize);

		fclose(file);
	}
}

void profilerFrameLoadError(const char* _name)
{
	printf("ERROR: Failed to load capture!");
}

void frameInspector();

void loop()
{
	int width = canvas_get_width();
	int height = canvas_get_height();

	glfwSetWindowSize(g_window, width, height);

	ImGui::SetCurrentContext(imgui);

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	rprofDrawFrame(&g_frame, 0, 0, false);
	rprofDrawStats(&g_frame);

	ImGui::Render();

	int display_w, display_h;
	glfwMakeContextCurrent(g_window);
	glfwGetFramebufferSize(g_window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(0.258f, 0.235f, 0.325f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwMakeContextCurrent(g_window);
}

extern "C" int main(int argc, char** argv)
{
	if (init() != 0) return 1;
#ifdef __EMSCRIPTEN__

	// path to capture passed via URL query, arguments are 'path' and 'file'
	// http://localhost/profile_inspector/imgui.html?path=http://localhost/profile_inspector/?file=capture.rprof

	char pathBuffer[1024];
	strcpy(pathBuffer,argv[1]);
	strcat(pathBuffer,argv[2]);

	printf("Path: %s\n", argv[1]);
	printf("File: %s\n", argv[2]);
	printf("Downloading %s\n", pathBuffer);

	emscripten_async_wget(pathBuffer, argv[2], profilerFrameLoadCallback, profilerFrameLoadError);

	printf("Download completed\n");

	emscripten_set_main_loop(loop, 0, 1);
#endif
	quit();
	return 0;
}
