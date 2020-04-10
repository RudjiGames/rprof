#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
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

EM_JS(int,	canvas_get_width, (),	{ return Module.canvas.width; });
EM_JS(int,	canvas_get_height, (),	{ return Module.canvas.height; });
EM_JS(void,	resizeCanvas, (),		{ js_resizeCanvas(); });

struct FrameInfo
{
	float		m_time;
	uint32_t	m_offset;
	uint32_t	m_size;
};

GLFWwindow*				g_window;
ImGuiContext*			imgui = 0;
int						g_multi = -1;
ProfilerFrame			g_frame;
char					g_fileName[1024];
std::vector<FrameInfo>	g_frameInfos;

struct SortFrameInfoChrono
{
	bool operator()(const FrameInfo& a, const FrameInfo& b) const
	{
		if (a.m_offset < b.m_offset) return true;
		return false;
	}
} customChrono;

struct SortFrameInfoDesc
{
	bool operator()(const FrameInfo& a, const FrameInfo& b) const
	{
		if (a.m_time > b.m_time) return true;
		return false;
	}
} customDesc;

struct SortFrameInfoAsc
{
	bool operator()(const FrameInfo& a, const FrameInfo& b) const
	{
		if (a.m_time < b.m_time) return true;
		return false;
	}
} customAsc;

void profilerFrameLoad(const char* _name, uint32_t _offset = 0, uint32_t _size = 0);

void rprofDrawTutorial(bool _multi)
{
	ImGui::SetNextWindowPos(ImVec2(10.0f, _multi ? 650.0f : 500.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(900.0f, 410.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin("Usage instructions");
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Hovering scopes");
	ImGui::Separator();
	ImGui::Text("Hovering a scope will display additional information about the scope (file, line and time).");
	ImGui::Text("Scpoes in 'Frame stats' are cumulative and will show number of occurences and frame time percentage in addition.");
	ImGui::NewLine();
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Zooming and panning");
	ImGui::Separator();
	ImGui::Text("Zoom in and zoom out of 'Frame inspector' is done using 'a' and 'z' keys.");
	ImGui::Text("Zooming is centered around the mouse X axis and mouse must be hovering the 'Frame inspector'.");
	ImGui::Text("Paning is can be done on a zoomed in 'Frame inspector' by holding down CTRL key and moving mouse left/right.");
	ImGui::NewLine();
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Clicking on scopes");
	ImGui::Separator();
	ImGui::Text("Clicking on a scope will highlight a scope (or all matching scopes) with the same name.");
	ImGui::Text("This helps to locate all occurences of a scope in a frame.");
	ImGui::NewLine();
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Multi frame view");
	ImGui::Separator();
	ImGui::Text("If capture is multi-frame, a 'Frame navigator' window will appear.");
	ImGui::Text("Clicking on a frame will load the profiling data for that particular frame.");

	ImGui::End();
}

void rprofDrawFrameNavigation(FrameInfo* _infos, uint32_t _numInfos)
{
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1510.0f, 140.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin("Frame navigator", 0, ImGuiWindowFlags_NoScrollbar);

	static int sortKind = 0;
	ImGui::Text("Sort frames by:  ");
	ImGui::SameLine();
	ImGui::RadioButton("Chronological", &sortKind, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Descending", &sortKind, 1);
	ImGui::SameLine();
	ImGui::RadioButton("Ascending", &sortKind, 2);
	ImGui::Separator();

	switch (sortKind)
	{
	case 0:
		std::sort(&_infos[0], &_infos[_numInfos], customChrono);
		break;

	case 1:
		std::sort(&_infos[0], &_infos[_numInfos], customDesc);
		break;

	case 2:
		std::sort(&_infos[0], &_infos[_numInfos], customAsc);
		break;
	};

	float maxTime = 0;
	for (uint32_t i=0; i<_numInfos; ++i)
	{
		if (maxTime < _infos[i].m_time)
			maxTime = _infos[i].m_time;
	}

	const ImVec2 s = ImGui::GetWindowSize();
	const ImVec2 p = ImGui::GetWindowPos();

	ImGui::BeginChild("", ImVec2(s.x, 70), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);

	int idx = ImGui::PlotHistogram("", (const float*)_infos, _numInfos, 0, "", 0.f, maxTime, ImVec2(_numInfos * 10, 50), sizeof(FrameInfo));

	if (ImGui::IsMouseClicked(0) && (idx != -1))
	{
		profilerFrameLoad(g_fileName, _infos[idx].m_offset, _infos[idx].m_size);
	}

	ImGui::EndChild();

	ImGui::End();
}

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

void quit()
{
	rprofRelease(&g_frame);
	glfwTerminate();
}

void profilerFrameLoad(const char* _name, uint32_t _offset, uint32_t _size)
{
	FILE* file = fopen(_name, "rb");
	if (file)
	{
		long csize = _size;

		if (!csize)
		{
			fseek(file, 0, SEEK_END);
			csize = ftell(file);
			fseek(file, 0, SEEK_SET);
		}
		else
			fseek(file, _offset + 4, SEEK_SET);

		static const size_t maxDecompSize = 4 * 1024 * 1024;
		char* compBuffer = (char*)malloc(csize);
		char* decompBuffer = (char*)malloc(maxDecompSize);
		char* bufferReadPtr = decompBuffer;

		fread(compBuffer, 1, csize, file);
		rprofLoad(&g_frame, compBuffer, csize);
		free(decompBuffer);
		free(compBuffer);

		fclose(file);
	}
}

void profilerFrameLoadMulti(const char* _name)
{
	strcpy(g_fileName, _name);
	FILE* file = fopen(_name, "rb");
	if (file)
	{
		fseek(file, 0, SEEK_END);
		long fileSize = ftell(file);
		fseek(file, 0, SEEK_SET);

		uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
		fread(fileBuffer, 1, fileSize, file);
		fclose(file);

		uint32_t offset = 4;
		while (offset < (uint32_t)fileSize)
		{
			FrameInfo info;
			info.m_offset = offset;

			uint32_t frameSize = *reinterpret_cast<uint32_t*>(&fileBuffer[offset]);
			offset += 4;

			rprofLoadTimeOnly(&info.m_time, &fileBuffer[offset], frameSize);

			info.m_size = frameSize;
			g_frameInfos.push_back(info);

			rprofRelease(&g_frame);
			offset += frameSize;
		}
	}

	profilerFrameLoad(g_fileName, g_frameInfos[0].m_offset, g_frameInfos[0].m_size);
}

void profilerFrameLoadCallback(const char* _name)
{
	FILE* file = fopen(_name, "rb");
	if (file)
	{
		uint32_t sig;
		fread(&sig, 1, 4, file);
		fclose(file);

		g_multi = sig == 0x23232323 ? 1 : 0;

		if (g_multi)
			profilerFrameLoadMulti(_name);
		else
			profilerFrameLoad(_name);
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

	if (g_multi != -1)
	{
		rprofDrawTutorial(g_multi == 1);

		if (g_multi)
			rprofDrawFrameNavigation(g_frameInfos.data(), g_frameInfos.size());

		rprofDrawFrame(&g_frame, 0, 0, false, g_multi == 1);
		rprofDrawStats(&g_frame, g_multi == 1);
	}

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
