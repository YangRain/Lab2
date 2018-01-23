#include <windows.h> // for XMVerifyCPUSupport
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <iostream>
#include <direct.h>
#include "Initialization.h"
#include "GameCode.h"
GameCodeApp *g_pApp = NULL;
#define GCC_ERROR(str) \
	do \
	{ \
		static Logger::ErrorMessenger* pErrorMessenger = GCC_NEW Logger::ErrorMessenger; \
		std::string s((str)); \
		pErrorMessenger->Show(s, false, __FUNCTION__, __FILE__, __LINE__); \
	} \
	while (0)\


#define GCC_ERROR(str) do { (void)sizeof(str); } while(0) 



GameCodeApp::GameCodeApp()
{
	g_pApp = this;
	m_pGame = NULL;

	m_rcDesktop.bottom = m_rcDesktop.left = m_rcDesktop.right = m_rcDesktop.top = 0;
	m_screenSize = Point(0, 0);
	m_iColorDepth = 32;

	m_bIsRunning = false;
	m_bIsEditorRunning = false;

	m_pEventManager = NULL;
	m_ResCache = NULL;

	m_pNetworkEventForwarder = NULL;
	m_pBaseSocketManager = NULL;

	m_bQuitRequested = false;
	m_bQuitting = false;
	m_HasModalDialog = 0;
}

bool GameCodeApp::InitInstance(HINSTANCE hInstance, LPWSTR lpCmdLine, HWND hWnd)
{
	// Check for existing instance of the same window
	// 
#ifndef _DEBUG
	// Note - it can be really useful to debug network code to have
	// more than one instance of the game up at one time - so
	// feel free to comment these lines in or out as you wish!
	if (!IsOnlyInstance(VGetGameTitle()))
		return false;
#endif

	// We don't need a mouse cursor by default, let the game turn it on
	SetCursor(NULL);

	// Check for adequate machine resources.
	bool resourceCheck = false;
	while (!resourceCheck)
	{
		const DWORDLONG physicalRAM = 512 * MEGABYTE;
		const DWORDLONG virtualRAM = 1024 * MEGABYTE;
		const DWORDLONG diskSpace = 10 * MEGABYTE;
		if (!CheckStorage(diskSpace))
			return false;

		const DWORD minCpuSpeed = 1300;			// 1.3Ghz
		DWORD thisCPU = ReadCPUSpeed();
		if (thisCPU < minCpuSpeed)
		{
			GCC_ERROR("GetCPUSpeed reports CPU is too slow for this game.");
			return false;
		}

		resourceCheck = true;
	}

	m_hInstance = hInstance;

	// register all events
	RegisterEngineEvents();
	VRegisterGameEvents();

	//
	// Initialize the ResCache - Chapter 5, page 141
	//
	//    Note - this is a little different from the book. Here we have a speccial resource ZIP file class, DevelopmentResourceZipFile,
	//    that actually reads directly from the source asset files, rather than the ZIP file. This is MUCH better during development, since
	//    you don't want to have to rebuild the ZIP file every time you make a minor change to an asset.
	//
	IResourceFile *zipFile = (m_bIsEditorRunning || m_Options.m_useDevelopmentDirectories) ?
		GCC_NEW DevelopmentResourceZipFile(L"Assets.zip", DevelopmentResourceZipFile::Editor) :
		GCC_NEW ResourceZipFile(L"Assets.zip");

	m_ResCache = GCC_NEW ResCache(50, zipFile);

	if (!m_ResCache->Init())
	{
		GCC_ERROR("Failed to initialize resource cache!  Are your paths set up correctly?");
		return false;
	}

	extern shared_ptr<IResourceLoader> CreateWAVResourceLoader();
	extern shared_ptr<IResourceLoader> CreateOGGResourceLoader();
	extern shared_ptr<IResourceLoader> CreateDDSResourceLoader();
	extern shared_ptr<IResourceLoader> CreateJPGResourceLoader();
	extern shared_ptr<IResourceLoader> CreateXmlResourceLoader();
	extern shared_ptr<IResourceLoader> CreateSdkMeshResourceLoader();
	extern shared_ptr<IResourceLoader> CreateScriptResourceLoader();

	// Note - register these in order from least specific to most specific! They get pushed onto a list.
	// RegisterLoader is discussed in Chapter 5, page 142
	m_ResCache->RegisterLoader(CreateWAVResourceLoader());
	m_ResCache->RegisterLoader(CreateOGGResourceLoader());
	m_ResCache->RegisterLoader(CreateDDSResourceLoader());
	m_ResCache->RegisterLoader(CreateJPGResourceLoader());
	m_ResCache->RegisterLoader(CreateXmlResourceLoader());
	m_ResCache->RegisterLoader(CreateSdkMeshResourceLoader());
	m_ResCache->RegisterLoader(CreateScriptResourceLoader());

	if (!LoadStrings("English"))
	{
		GCC_ERROR("Failed to load strings");
		return false;
	}

	// Rez up the Lua State manager now, and run the initial script - discussed in Chapter 5, page 144.
	if (!LuaStateManager::Create())
	{
		GCC_ERROR("Failed to initialize Lua");
		return false;
	}

	// Load the preinit file.  This is within braces to create a scope and destroy the resource once it's loaded.  We
	// don't need to do anything with it, we just need to load it.
	{
		Resource resource(SCRIPT_PREINIT_FILE);
		shared_ptr<ResHandle> pResourceHandle = m_ResCache->GetHandle(&resource);  // this actually loads the XML file from the zip file
	}

	// Register function exported from C++
	ScriptExports::Register();
	ScriptProcess::RegisterScriptClass();
	BaseScriptComponent::RegisterScriptFunctions();

	// The event manager should be created next so that subsystems can hook in as desired.
	// Discussed in Chapter 5, page 144
	m_pEventManager = GCC_NEW EventManager("GameCodeApp Event Mgr", true);
	if (!m_pEventManager)
	{
		GCC_ERROR("Failed to create EventManager.");
		return false;
	}

	// DXUTInit, DXUTCreateWindow - Chapter 5, page 145	
	DXUTInit(true, true, lpCmdLine, true); // Parse the command line, handle the default hotkeys, and show msgboxes

	if (hWnd == NULL)
	{
		DXUTCreateWindow(VGetGameTitle(), hInstance, VGetIcon());
	}
	else
	{
		DXUTSetWindow(hWnd, hWnd, hWnd);
	}

	if (!GetHwnd())
	{
		return FALSE;
	}
	SetWindowText(GetHwnd(), VGetGameTitle());

	// initialize the directory location you can store save game files
	_tcscpy_s(m_saveGameDirectory, GetSaveGameDirectory(GetHwnd(), VGetGameAppDirectory()));

	// DXUTCreateDevice - Chapter 5 - page 139
	m_screenSize = Point(screenWidth, screenHeight);

	//If you have an older video card that only supports D3D9, comment in the next line, and make sure 
	//the renderer setting in Game\PlayerOptions.xml is set to "Direct3D 9"
	//DXUTCreateDevice( D3D_FEATURE_LEVEL_9_3, true, screenWidth, screenHeight);
	DXUTCreateDevice(D3D_FEATURE_LEVEL_10_1, true, screenWidth, screenHeight);

	if (GetRendererImpl() == Renderer_D3D9)
	{
		m_Renderer = shared_ptr<IRenderer>(GCC_NEW D3DRenderer9());
	}
	else if (GetRendererImpl() == Renderer_D3D11)
	{
		m_Renderer = shared_ptr<IRenderer>(GCC_NEW D3DRenderer11());
	}
	m_Renderer->VSetBackgroundColor(255, 20, 20, 200);
	m_Renderer->VOnRestore();


	// You usually must have an HWND to initialize your game views...
	//    VCreateGameAndView			- Chapter 5, page 145
	m_pGame = VCreateGameAndView();
	if (!m_pGame)
		return false;

	// now that all the major systems are initialized, preload resources 
	//    Preload calls are discussed in Chapter 5, page 148
	m_ResCache->Preload("*.ogg", NULL);
	m_ResCache->Preload("*.dds", NULL);
	m_ResCache->Preload("*.jpg", NULL);

	if (GameCodeApp::GetRendererImpl() == GameCodeApp::Renderer_D3D11)
		m_ResCache->Preload("*.sdkmesh", NULL);

	CheckForJoystick(GetHwnd());

	m_bIsRunning = true;

	return TRUE;
}


bool GameCodeApp::LoadStrings(std::string language)
{
	std::string languageFile = "Strings\\";
	languageFile += language;
	languageFile += ".xml";

	TiXmlElement* pRoot = XmlResourceLoader::LoadAndReturnRootXmlElement(languageFile.c_str());
	if (!pRoot)
	{
		GCC_ERROR("Strings are missing.");
		return false;
	}

	// Loop through each child element and load the component
	for (TiXmlElement* pElem = pRoot->FirstChildElement(); pElem; pElem = pElem->NextSiblingElement())
	{
		const char *pKey = pElem->Attribute("id");
		const char *pText = pElem->Attribute("value");
		const char *pHotkey = pElem->Attribute("hotkey");
		if (pKey && pText)
		{
			wchar_t wideKey[64];
			wchar_t wideText[1024];
			AnsiToWideCch(wideKey, pKey, 64);
			AnsiToWideCch(wideText, pText, 1024);
			m_textResource[std::wstring(wideKey)] = std::wstring(wideText);

			if (pHotkey)
			{
				m_hotkeys[std::wstring(wideKey)] = MapCharToKeycode(*pHotkey);
			}
		}
	}
	return true;
}



LRESULT GameCodeApp::OnClose()
{
	// release all the game systems in reverse order from which they were created

	SAFE_DELETE(m_pGame);

	DestroyWindow(GetHwnd());

	VDestroyNetworkEventForwarder();

	SAFE_DELETE(m_pBaseSocketManager);

	SAFE_DELETE(m_pEventManager);

	BaseScriptComponent::UnregisterScriptFunctions();
	ScriptExports::Unregister();
	LuaStateManager::Destroy();

	SAFE_DELETE(m_ResCache);

	return 0;
}



void GameCodeApp::FlashWhileMinimized()
{
	// Flash the application on the taskbar
	// until it's restored.
	if (!GetHwnd())
		return;

	// Blink the application if we are minimized,
	// waiting until we are no longer minimized
	if (IsIconic(GetHwnd()))
	{
		
		DWORD now = timeGetTime();
		DWORD then = now;
		MSG msg;

		FlashWindow(GetHwnd(), true);

		while (true)
		{
			if (PeekMessage(&msg, NULL, 0, 0, 0))
			{
				if (msg.message != WM_SYSCOMMAND || msg.wParam != SC_CLOSE)
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}

				// Are we done?
				if (!IsIconic(GetHwnd()))
				{
					FlashWindow(GetHwnd(), false);
					break;
				}
			}
			else
			{
				now = timeGetTime();
				DWORD timeSpan = now > then ? (now - then) : (then - now);
				if (timeSpan > 1000)
				{
					then = now;
					FlashWindow(GetHwnd(), true);
				}
			}
		}
	}
}

