//--------------------------------------------------------------------------//
/// Copyright 2023 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <demo_pch.h>
#include <rprof/inc/rprof_imgui.h>

static int random(int max)
{
	return 1 + rand() % (max>1?max-1:1);
}

static void busyCPU()
{
	uint32_t loopCnt = (rand() % 10000);
	for (uint32_t i=0; i<loopCnt; ++i)
		rand();
}

static void func(int level=1)
{
	static char scopeName[6];
	static bool scopeNameInit = false;
	if (!scopeNameInit)
	{
		strcpy(scopeName, "funcX");
		scopeNameInit = true;
	}

	scopeName[4] = '0' + (char)level;

	RPROF_SCOPE(scopeName);
	busyCPU();
	if (level < 5)
	{
		int cnt = random(3);
		for (int i=0; i<cnt; ++i)
			func(level+1);
		busyCPU();
	}
}

struct rprofApp : public rapp::App
{
	RAPP_CLASS(rprofApp)

	rapp::WindowHandle	m_window;
	float				m_time;

	virtual ~rprofApp()
	{
		RPROF_SHUTDOWN();
	}

	int init(int32_t /*_argc*/, const char* const* /*_argv*/, rtmLibInterface* /*_libInterface = 0*/)
	{
		m_time = 0.0f;

		static const rapp::InputBinding bindings[] =
		{
			{ NULL, "exit", 1, { rapp::KeyboardState::Key::KeyQ,   rapp::KeyboardState::Modifier::LCtrl  }},
			{ NULL, "exit", 1, { rapp::KeyboardState::Key::KeyQ,   rapp::KeyboardState::Modifier::RCtrl  }},
			{ NULL, "hide", 1, { rapp::KeyboardState::Key::Tilde,  rapp::KeyboardState::Modifier::NoMods }},
			RAPP_INPUT_BINDING_END
		};

		rapp::inputAddBindings("bindings", bindings);
		rapp::cmdAdd("exit", cmdExit,			0, "quits application");
		rapp::cmdAdd("hide", cmdHideConsole,	0, "hides console");

		uint32_t width, height;
		rapp::windowGetDefaultSize(&width, &height);
		m_width		= width;
		m_height	= height;

		rapp::appGraphicsInit(this, m_width, m_height);

		RPROF_INIT();
		RPROF_REGISTER_THREAD("Application thread");
		appRunOnMainThread(registerMainThread, this);
		return 0;
	}

	void suspend() {}
	void resume() {}
	void update(float _time)
	{
		RPROF_SCOPE("Update");
		m_time += _time;
		busyCPU();
	}

	void draw()
	{
		RPROF_BEGIN_FRAME();
		appRunOnMainThread(mainThreadFunc, this);

		func();
		busyCPU();
		func();

		// Use debug font to print information about this example.
		bgfx::dbgTextClear();
		bgfx::dbgTextPrintf(0, 1, 0x17, "rprof/samples/demo");
		bgfx::dbgTextPrintf(0, 2, 0x37, m_description);
	}

	void drawGUI()
	{
		RPROF_SCOPE("Draw GUI");
		ProfilerFrame data;
		rprofGetFrame(&data);

		char buffer[10*1024];
		if (int size = rprofDrawFrame(&data, buffer, 10*1024))
		{
			FILE* file = fopen("capture.rprof", "wb");
			if (file)
			{
				fwrite(buffer, 1, size, file);
				fclose(file);
			}

			ProfilerFrame data2;
			rprofLoad(&data2, buffer, size);
			data2.m_CPUFrequency = 0;
		}

		// Example of writing multi-frame data

		/*
		static bool headerWritten = false;
		FILE* file = 0;
		if (!headerWritten)
		{
			uint32_t sig = 0x23232323;
			file = fopen("capture.rprofm", "wb");
			fwrite(&sig, 1, 4, file);
			fclose(file);
			headerWritten = true;
		}

		uint32_t size = rprofSave(&data, buffer, 10 * 1024);
		file = fopen("capture.rprofm", "ab");
		fwrite(&size, 4, 1, file);
		fwrite(buffer, 1, size, file);
		fclose(file);
		*/
	}
	
	void shutDown()
	{
		rtm::Console::rgb(255, 255, 0, "Shutting down app\n");
		rapp::appGraphicsShutdown(this, m_window);
		rapp::inputRemoveBindings("bindings");
	}

	static void mainThreadFunc(void* /*_appClass*/)
	{
		RPROF_SCOPE("main thread");
		func();
	}

	static void registerMainThread(void* /*_appClass*/)
	{
		RPROF_REGISTER_THREAD("Main thread");
	}

	static int cmdExit(rapp::App* _app, void* _userData, int _argc, char const* const* _argv)
	{
		RTM_UNUSED_3(_userData, _argv, _argc);
		_app->quit();
		return 0;
	}

	static int cmdHideConsole(rapp::App* _app, void* _userData, int _argc, char const* const* _argv)
	{
		RTM_UNUSED_3(_userData, _argv, _argc);
		rapp::cmdConsoleToggle(_app);
		return 0;
	}
};

RAPP_REGISTER(rprofApp, "rprof", "Example of rprof ImGui visualization");
