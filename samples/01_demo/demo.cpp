//--------------------------------------------------------------------------//
/// Copyright (c) 2018 Milos Tosic. All Rights Reserved.                   ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <demo_pch.h>
#include <rprof/inc/rprof_imgui.h>

int random(int max)
{
	return 1 + rand() % (max>1?max-1:1);
}

static void func4()
{
	RPROF_SCOPE("func1");
	rtm::Thread::sleep(random(1));
}

static void func3()
{
	RPROF_SCOPE("func3");
	rtm::Thread::sleep(random(2));
	for (int i=0; i<random(3); ++i)
		func4();
}

static void func2()
{
	RPROF_SCOPE("func2");
	rtm::Thread::sleep(random(2));
	for (int i=0; i<random(3); ++i)
		func3();
}

static void func1()
{
	RPROF_SCOPE("func1");
	rtm::Thread::sleep(random(2));
	func2();
	rtm::Thread::sleep(random(2));
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
		rtm::Thread::sleep(random(1));
	}

	void draw()
	{
		RPROF_BEGIN_FRAME();
		appRunOnMainThread(mainThreadFunc, this);

		func1();
		rtm::Thread::sleep(random(2));
		func1();

		// Set view 0 default viewport.
		bgfx::setViewRect(0, 0, 0, (uint16_t)m_width, (uint16_t)m_height);

		// This dummy draw call is here to make sure that view 0 is cleared
		// if no other draw calls are submitted to view 0.
		bgfx::touch(0);

		// Use debug font to print information about this example.
		bgfx::dbgTextClear();
		bgfx::dbgTextPrintf(0, 1, 0x17, "rprof/samples/demo");
		bgfx::dbgTextPrintf(0, 2, 0x37, m_description);

		// Advance to next frame. Rendering thread will be kicked to 
		// process submitted rendering primitives.
		bgfx::frame();
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
		rtm::Thread::sleep(random(12));
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
