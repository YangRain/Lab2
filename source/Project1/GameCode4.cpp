#include <windows.h> // for XMVerifyCPUSupport
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <iostream>
#include <direct.h>
#include "string.h"
#include "GameCode.h"
#include "DXUT.h"

using namespace std;
using namespace DirectX;
using namespace DirectX::PackedVector;

namespace Logger
{
	// This class is used by the debug macros and shouldn't be accessed externally.
	class ErrorMessenger
	{
		bool m_enabled;

	public:
		ErrorMessenger(void);
		void Show(const std::string& errorMessage, bool isFatal, const char* funcName, const char* sourceFile, unsigned int lineNum);
	};

	// construction; must be called at the beginning and end of the program
	void Init(const char* loggingConfigFilename);
	void Destroy(void);

	// logging functions
	void Log(const std::string& tag, const std::string& message, const char* funcName, const char* sourceFile, unsigned int lineNum);
	void SetDisplayFlags(const std::string& tag, unsigned char flags);
}



INT WINAPI GameCode4(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPWSTR    lpCmdLine,
	int       nCmdShow)
{
	// Set up checks for memory leaks.
	// Game Coding Complete reference - Chapter 21, page 834
	//
	int tmpDbgFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);


	//_CRTDBG_LEAK_CHECK_DF is used at program initialization to force a 
	//   leak check just before program exit. This is important because
	//   some classes may dynamically allocate memory in globally constructed
	//   objects.
	tmpDbgFlag |= _CRTDBG_LEAK_CHECK_DF;

	_CrtSetDbgFlag(tmpDbgFlag);

	// [rez] Initialize the logging system as the very first thing you ever do!
	// [mrmike] LOL after the memory system flags are set, that is!
	Logger::Init("logging.xml");

	g_pApp->m_Options.Init("PlayerOptions.xml", lpCmdLine);


		DXUTSetCallbackD3D11DeviceAcceptable(GameCodeApp::IsD3D11DeviceAcceptable);
		DXUTSetCallbackD3D11DeviceCreated(GameCodeApp::OnD3D11CreateDevice);
		DXUTSetCallbackD3D11SwapChainResized(GameCodeApp::OnD3D11ResizedSwapChain);
		DXUTSetCallbackD3D11SwapChainReleasing(GameCodeApp::OnD3D11ReleasingSwapChain);
		DXUTSetCallbackD3D11DeviceDestroyed(GameCodeApp::OnD3D11DestroyDevice);
		DXUTSetCallbackD3D11FrameRender(GameCodeApp::OnD3D11FrameRender);

	// Show the cursor and clip it when in full screen
	DXUTSetCursorSettings(true, true);

	// Perform application initialization
	if (!g_pApp->InitInstance(hInstance, lpCmdLine, 0, g_pApp->m_Options.m_ScreenSize.x, g_pApp->m_Options.m_ScreenSize.y))
	{
		// [rez] Note: Fix memory leaks if we hit this branch.  Also, print an error.
		return FALSE;
	}

	// Pass control to the sample framework for handling the message pump and 
	// dispatching render calls. The sample framework will call your FrameMove 
	// and FrameRender callback when there is idle time between handling window messages.

	DXUTMainLoop();
	DXUTShutdown();

	// [rez] Destroy the logging system at the last possible moment
	Logger::Destroy();

	return g_pApp->GetExitCode();
}
