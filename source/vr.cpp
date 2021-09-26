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
//
// _game_sharedhandle cannot be be properly disposed because it's not a real handle. More
// Microsoft genius APIs.

void vr::DestroySharedTexture()
{
	LOG(INFO) << "vr:DX11 DestroySharedTexture called. _shared_texture: " << _shared_texture << " _game_sharedhandle: " << _game_sharedhandle << " _mapped_view: " << _mapped_view;

	// Save possible prior usage to be disposed after we recreate.
	IUnknown* oldGameTexture = _shared_texture;

	// Tell Katanga it's gone, so it can drop its buffers.
	_game_sharedhandle = NULL;
	*(PUINT)(_mapped_view) = PtrToUint(_game_sharedhandle);

	if (oldGameTexture)
	{
		LOG(INFO) << "  Release stale _shared_texture: " << oldGameTexture;

		oldGameTexture->Release();
		_shared_texture = nullptr;
	}
}

