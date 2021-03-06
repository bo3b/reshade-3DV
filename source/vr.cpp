// This code is for the VR connection to HelixVision.
//
// Copied from 3Dmigoto version with HelixVision, but modified to remove all nvidia
// 3D Vision requirements.
//
// This is the base class for the vr objects, with subclasses
// for the DX11 and DX12 use cases, to handle the variance in the texture
// structures being passed.  The output double-width texture to Katanga
// will remain as a DX11 object as Katanga will remain DX11, and using
// surface sharing, we can copy into that DX11 Texture 2D.

#include "vr.hpp"

#include "dll_log.hpp"
#include "dxgi/format_utils.hpp"

// -----------------------------------------------------------------------------

vr::vr()
{
}

vr::~vr()
{
}



// ----------------------------------------------------------------------
// Fatal error handling.  This is for scenarios that should never happen,
// but should be checked just for reliability and unforeseen scenarios.
//
// We tried using std::exception, but that was mostly useless because nearly every
// game has an exception handler of some form that wraps and silently kills any
// exceptions, which looked to the end-user as just a game-hang.  
//
// This attempt here is to put up a MessageBox with the informative error text
// and exit the game.  This should be better than simple logging, because at
// least the user gets immediate notification and does not have to sift around
// to find log files.  

DECLSPEC_NORETURN void vr::DoubleBeepExit()
{
	// Fatal error somewhere, known to crash, might as well exit cleanly
	// with some notification.
	// Brnk, dunk sound for failure.
	Beep(300, 200); Beep(200, 150);
	Sleep(500);
	// Brnk, dunk sound for failure.
	Beep(300, 200); Beep(200, 150);
	Sleep(200);

	//if (LogFile) {
	//	// Make sure the log is written out so we see the failure message
	//	fclose(LogFile);
	//	LogFile = 0;
	//}
	ExitProcess(0xc0000135);
}

void vr::FatalExit(LPCWSTR errorString, HRESULT code)
{
	wchar_t info[512];

	LOG(ERROR) << " Fatal Error: " << errorString << " code: " << code;

	swprintf_s(info, 512, L"Report: %s\n\nError: %x", errorString, code);

	MessageBox(NULL, info, L"GamePlugin: Fatal Error", MB_OK);
	DoubleBeepExit();
}

// -----------------------------------------------------------------------------

// Creates the file mapped IPC to share the _game_sharedhandle with the VR/Unity/Katanga
// side, so that it can display the shared surface.  The only thing we need for this
// is the Handle, from which a shared surface is created.  The Handle is written
// into the _mapped_view here, and read in Katanga.

void vr::CreateFileMappedIPC()
{
	TCHAR szName[] = TEXT("Local\\KatangaMappedFile");

	_mapped_file = CreateFileMapping(
		INVALID_HANDLE_VALUE,			// use paging file
		NULL,							// default security
		PAGE_READWRITE,					// read/write access
		0,								// maximum object size (high-order DWORD)
		sizeof(UINT),					// maximum object size (low-order DWORD)
		szName);						// name of mapping object
	if (_mapped_file == NULL) FatalExit(L"vr: could not CreateFileMapping for VR IPC", GetLastError());

	_mapped_view = MapViewOfFile(
		_mapped_file,					// handle to map file object
		FILE_MAP_ALL_ACCESS,			// read/write permission
		0,								// No offset in file
		0,
		sizeof(UINT));					// Map full buffer
	if (_mapped_view == NULL) FatalExit(L"vr: could not MapViewOfFile for VR IPC", GetLastError());

	LOG(INFO) << "vr: Mapped file created for VR IPC: " << _mapped_view << "->" << *(UINT*)(_mapped_view);
}

void vr::ReleaseFileMappedIPC()
{
	LOG(INFO) << "vr: Unmap file for " << _mapped_file;
	if (_mapped_file != NULL)
	{
		UnmapViewOfFile(_mapped_view);
		CloseHandle(_mapped_file);
	}
}

//-----------------------------------------------------------

// Setup shared mutex, either side can have created it, because we don't
// know which will launch first.
// We will grab mutex to lock their drawing, whenever we are setting up
// the shared surface.  This is created only once during CaptureSetupMutex
// as a late binding.  Both sides call CreateMutex, so we don't need
// to verify which happens first.

void vr::CreateCaptureMutex()
{
	_setup_mutex = CreateMutex(nullptr, false, L"KatangaSetupMutex");

	LOG(INFO) << "vr: CreateMutex called: " << _setup_mutex;

	if (_setup_mutex == NULL)
		FatalExit(L"vr: could not find KatangaSetupMutex", GetLastError());
}

void vr::DisposeCaptureMutex()
{
	LOG(INFO) << "vr: CloseHandle on Mutex called: " << _setup_mutex;

	if (_setup_mutex != NULL)
		CloseHandle(_setup_mutex);
}

// Used for blocking around the CopySubResourceRegion for updating the frame.
// Does not block contention, that seems to not be a problem, but it can help
// keep the game side and katanga in better sync, so that katanga is not starved.
//
// We should be OK using a 1 second wait here, if we cannot grab the mutex from the
// Katanga side in 1 second, something is definitely broken.

void vr::CaptureSetupMutex()
{
	DWORD waitResult;

	if (_setup_mutex == NULL)
		CreateCaptureMutex();

	LOG(DEBUG) << "-> CaptureSetupMutex mutex:" << _setup_mutex;

	waitResult = WaitForSingleObject(_setup_mutex, 1000);
	if (waitResult != WAIT_OBJECT_0)
	{
		wchar_t info[512];
		DWORD hr = GetLastError();
		swprintf_s(info, _countof(info), L"CaptureSetupMutex: WaitForSingleObject failed.\nwaitResult: 0x%x, err: 0x%x\n", waitResult, hr);
		LOG(ERROR) << info;
		FatalExit(info, hr);
	}

	LOG(DEBUG) << "  WaitForSingleObject mutex: " << _setup_mutex << ", result: " << waitResult;
}

// Release use of shared mutex, so the VR side can grab the mutex, and thus know that
// it can fetch the shared surface and use it to draw.  Normal operation is that the
// VR side grabs and releases the mutex for every frame, and is only blocked when
// we are setting up the graphics environment here, either as first run where this
// side creates the mutex as active and locked, or when Reset/Resize is called and we grab
// the mutex to lock out the VR side.

void vr::ReleaseSetupMutex()
{
	LOG(DEBUG) << "<- ReleaseSetupMutex mutex:" << _setup_mutex;

	BOOL ok = ReleaseMutex(_setup_mutex);
	if (!ok)
	{
		DWORD hr = GetLastError();
		LOG(INFO) << "ReleaseSetupMutex: ReleaseMutex failed, err: 0x%x\n", hr;
	}

	LOG(DEBUG) << "  ReleaseSetupMutex mutex: " << _setup_mutex << ", result: " <<  (ok ? "OK" : "FAIL");
}

// -----------------------------------------------------------------------------

// When the DoubleTex is unloaded, our shared handle becomes invalid.  This can happen
// if they turn off the effect, but also when the game calls ResizeBuffers.  It will
// be rebuilt automatically when the CaptureVRFrame is next called. Setting _game_sharedhandle
// to null, tells the Katanga side to switch to grey screen and stop any use of the old share. 

void vr::DestroySharedTexture()
{
	LOG(INFO) << "vr:DX11 DestroySharedTexture called. _shared_texture: " << _shared_texture << " _game_sharedhandle: " << _game_sharedhandle << " _mapped_view: " << _mapped_view;

	// Save possible prior usage to be disposed after we recreate.
	ID3D11Texture2D* oldGameTexture = _shared_texture;

	// Tell Katanga it's gone, so it can drop its buffers.
	_game_sharedhandle = NULL;
	*(PUINT)(_mapped_view) = PtrToUint(_game_sharedhandle);

	LOG(INFO) << "  Release stale _shared_texture: " << oldGameTexture;
	if (oldGameTexture)
	{
		oldGameTexture->Release();
		_shared_texture = nullptr;
	}
}

// Shared code for when we need to create the offscreen Texture2D for our stereo
// copy.  This is created when the CreateSwapChain is called, and also whenever
// it ResizeBuffers is called, because we need to change our destination copy
// to always match what the game is drawing. Buffer width is already 2x game size,
// as it comes from SuperDepth doubleTex.
//
// This will also rewrite the global _game_sharedhandle with a new HANDLE as
// needed, and the Unity side is expected to notice a change and setup a new
// drawing texture as well.  This is thus polling on the Unity side, which is
// not ideal, but it is one call here to fetch the 4 byte HANDLE every 11ms.  
// There does not appear to be a good way for this code to notify the C# code,
// although using a TriggerEvent with some C# interop might work.  We'll only
// do that work if this proves to be a problem.

void vr::CreateSharedTexture(ID3D11Texture2D* gameDoubleTex)
{
	HRESULT hr;
	D3D11_TEXTURE2D_DESC desc = { 0 };
	ID3D11Texture2D* oldGameTexture = _shared_texture;
	ID3D11Device* pDevice = nullptr;
	gameDoubleTex->GetDesc(&desc);
	gameDoubleTex->GetDevice(&pDevice);

	LOG(INFO) << "vr::CreateSharedTexture called. _shared_texture: " << _shared_texture << " _game_sharedhandle: " << _game_sharedhandle << " _mapped_view: " << _mapped_view;

	LOG(INFO) << "  | DoubleTex                               |                                         |";
	LOG(INFO) << "  +-----------------------------------------+-----------------------------------------+";
	LOG(INFO) << "  | Width                                   | " << std::setw(39) << desc.Width << " |";
	LOG(INFO) << "  | Height                                  | " << std::setw(39) << desc.Height << " |";
	if (const char *format_string = format_to_string(desc.Format); format_string != nullptr)
		LOG(INFO) << "  | Format                                  | " << std::setw(39) << format_string << " |";
	else
		LOG(INFO) << "  | Format                                  | " << std::setw(39) << desc.Format << " |";
	LOG(INFO) << "  +-----------------------------------------+-----------------------------------------+";

	// Some games like TheSurge and Dishonored2 will specify a DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
	// as their backbuffer.  This doesn't work for us because our output is going to the VR HMD,
	// and thus we get a doubled up sRGB/gamma curve, which makes it too dark, and the in-game
	// slider doesn't have enough range to correct.  
	// If we get one of these sRGB formats, we are going to strip that and return the Linear
	// version instead, so that we avoid this problem.  This allows us to use Gamma for the Unity
	// app itself, which matches 90% of the games, and still handle these oddball games automatically.

	if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

	// Reshade makes all buffers TypeLess, but we need the actual game type when creating a
	// shared surface.

	desc.Format = make_dxgi_format_normal(desc.Format);

	LOG(INFO) << "  | Final Format                            | " << std::setw(39) << format_to_string(desc.Format) << " |";

	// This texture needs to use the Shared flag, so that we can share it to 
	// another Device.  Because these are all DX11 objects, the share will work.

	desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;	// Must add bind flag, so SRV can be created in Unity.
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;	// To be shared. maybe D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX is better
													// But we never seem to see any contention between game and Katanga.
	
	hr = pDevice->CreateTexture2D(&desc, NULL, &_shared_texture);
	if (FAILED(hr)) FatalExit(L"Fail to create shared stereo Texture", hr);

	// Now create the HANDLE which is used to share surfaces.  This follows the model from:
	// https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/nf-d3d11-id3d11device-opensharedresource

	IDXGIResource* pDXGIResource = NULL;
	hr = _shared_texture->QueryInterface(__uuidof(IDXGIResource), (LPVOID*)&pDXGIResource);
	{
		if (FAILED(hr))	FatalExit(L"Fail to QueryInterface on shared surface", hr);

		hr = pDXGIResource->GetSharedHandle(&_game_sharedhandle);
		if (FAILED(hr) || _game_sharedhandle == NULL) FatalExit(L"Fail to pDXGIResource->GetSharedHandle", hr);
	}
	pDXGIResource->Release();

	LOG(INFO) << "  Successfully created new shared _shared_texture: " << _shared_texture << ", new shared _game_sharedhandle: " << _game_sharedhandle;

	// Move that shared handle into the MappedView to IPC the Handle to Katanga.
	// The HANDLE is always 32 bit, even for 64 bit processes.
	// https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication
	//
	// This magic line of code will fire off the Katanga side rebuilding of it's drawing pipeline and surface.

	*(PUINT)(_mapped_view) = PtrToUint(_game_sharedhandle);

	// If we already had created one, let the old one go.  We do it after the recreation
	// here fills in the prior globals, to avoid possible dead structure usage in the
	// Unity app.

	LOG(INFO) << "  Release stale _shared_texture: " << oldGameTexture;
	if (oldGameTexture)
		oldGameTexture->Release();
}
