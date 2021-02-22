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

static DECLSPEC_NORETURN void DoubleBeepExit()
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

static void FatalExit(LPCWSTR errorString, HRESULT code)
{
	wchar_t info[512];

	LOG(ERROR) << " Fatal Error: " << errorString << " code: " << code;

	MessageBox(NULL, info, L"GamePlugin: Fatal Error", MB_OK);
	DoubleBeepExit();
}

// -----------------------------------------------------------------------------

// Creates the file mapped IPC to share the gGameSharedHandle with the VR/Unity/Katanga
// side, so that it can display the shared surface.  The only thing we need for this
// is the Handle, from which a shared surface is created.  The Handle is written
// into the gMappedView here, and read in Katanga.

void vr::CreateFileMappedIPC()
{
	TCHAR szName[] = TEXT("Local\\KatangaMappedFile");

	gMappedFile = CreateFileMapping(
		INVALID_HANDLE_VALUE,			// use paging file
		NULL,							// default security
		PAGE_READWRITE,					// read/write access
		0,								// maximum object size (high-order DWORD)
		sizeof(UINT),					// maximum object size (low-order DWORD)
		szName);						// name of mapping object
	if (gMappedFile == NULL) FatalExit(L"vr: could not CreateFileMapping for VR IPC", GetLastError());

	gMappedView = MapViewOfFile(
		gMappedFile,					// handle to map file object
		FILE_MAP_ALL_ACCESS,			// read/write permission
		0,								// No offset in file
		0,
		sizeof(UINT));					// Map full buffer
	if (gMappedView == NULL) FatalExit(L"vr: could not MapViewOfFile for VR IPC", GetLastError());

	LOG(INFO) << "vr: Mapped file created for VR IPC: " << gMappedView << "->" << *(UINT*)(gMappedView);
}

void vr::ReleaseFileMappedIPC()
{
	LOG(INFO) << "vr: Unmap file for " << gMappedFile;
	if (gMappedFile != NULL)
	{
		UnmapViewOfFile(gMappedView);
		CloseHandle(gMappedFile);
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
	gSetupMutex = CreateMutex(nullptr, false, L"KatangaSetupMutex");

	LOG(INFO) << "vr: CreateMutex called: " << gSetupMutex;

	if (gSetupMutex == NULL)
		FatalExit(L"vr: could not find KatangaSetupMutex", GetLastError());
}

void vr::DisposeCaptureMutex()
{
	LOG(INFO) << "vr: CloseHandle on Mutex called: " << gSetupMutex;

	if (gSetupMutex != NULL)
		CloseHandle(gSetupMutex);
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

	if (gSetupMutex == NULL)
		CreateCaptureMutex();

	LOG(DEBUG) << "-> CaptureSetupMutex mutex:" << gSetupMutex;

	waitResult = WaitForSingleObject(gSetupMutex, 1000);
	if (waitResult != WAIT_OBJECT_0)
	{
		wchar_t info[512];
		DWORD hr = GetLastError();
		swprintf_s(info, _countof(info), L"CaptureSetupMutex: WaitForSingleObject failed.\nwaitResult: 0x%x, err: 0x%x\n", waitResult, hr);
		LOG(ERROR) << info;
		FatalExit(info, hr);
	}

	LOG(DEBUG) << "  WaitForSingleObject mutex: " << gSetupMutex << ", result: " << waitResult;
}

// Release use of shared mutex, so the VR side can grab the mutex, and thus know that
// it can fetch the shared surface and use it to draw.  Normal operation is that the
// VR side grabs and releases the mutex for every frame, and is only blocked when
// we are setting up the graphics environment here, either as first run where this
// side creates the mutex as active and locked, or when Reset/Resize is called and we grab
// the mutex to lock out the VR side.

void vr::ReleaseSetupMutex()
{
	LOG(DEBUG) << "<- ReleaseSetupMutex mutex:" << gSetupMutex;

	BOOL ok = ReleaseMutex(gSetupMutex);
	if (!ok)
	{
		DWORD hr = GetLastError();
		LOG(INFO) << "ReleaseSetupMutex: ReleaseMutex failed, err: 0x%x\n", hr;
	}

	LOG(DEBUG) << "  ReleaseSetupMutex mutex: " << gSetupMutex << ", result: " <<  (ok ? "OK" : "FAIL");
}

// -----------------------------------------------------------------------------

// When the DoubleTex is unloaded, our shared handle becomes invalid.  This can happen
// if they turn off the effect, but also when the game calls ResizeBuffers.  It will
// be rebuilt automatically when the CaptureVRFrame is next called.

void vr::DestroySharedTexture()
{
	LOG(INFO) << "vr:DX11 DestroySharedTexture called. gGameTexture: " << gGameTexture << " gGameSharedHandle: " << gGameSharedHandle << " gMappedView: " << gMappedView;

	// Save possible prior usage to be disposed after we recreate.
	ID3D11Texture2D* oldGameTexture = gGameTexture;

	// Tell Katanga it's gone, so it can drop its buffers.
	gGameSharedHandle = NULL;
	*(PUINT)(gMappedView) = PtrToUint(gGameSharedHandle);

	LOG(INFO) << "  Release stale gGameTexture: " << oldGameTexture;
	if (oldGameTexture)
		oldGameTexture->Release();
}

// Shared code for when we need to create the offscreen Texture2D for our stereo
// copy.  This is created when the CreateSwapChain is called, and also whenever
// it ResizeBuffers is called, because we need to change our destination copy
// to always match what the game is drawing.
//
// This will also rewrite the global gGameSharedHandle with a new HANDLE as
// needed, and the Unity side is expected to notice a change and setup a new
// drawing texture as well.  This is thus polling on the Unity side, which is
// not ideal, but it is one call here to fetch the 4 byte HANDLE every 11ms.  
// There does not appear to be a good way for this code to notify the C# code,
// although using a TriggerEvent with some C# interop might work.  We'll only
// do that work if this proves to be a problem.

void vr::CreateSharedTexture(ID3D11Texture2D* doubleTex)
{
	HRESULT hr;
	ID3D11Device* pDevice;
	D3D11_TEXTURE2D_DESC desc;
	ID3D11Texture2D* oldGameTexture = nullptr;

	LOG(INFO) << "vr:DX11 CreateSharedTexture called. gGameTexture: " << gGameTexture << " gGameSharedHandle: " << gGameSharedHandle << " gMappedView: " << gMappedView;

	// At first Present or ResizeBuffer call, we'll build the file mapped IPC.  Could conceivably
	// be done at 3Dmigoto startup.
	if (gMappedView == nullptr)
		CreateFileMappedIPC();


	// When called back and gGameSharedHandle exists, we are thus recreating a new shared texture, probably
	// as part of ResizeBuffers, but can be from Present because some games call ResizeBuffers 5 times before
	// calling Present.  Upon desired recreation, we will immediately mark the old one as defunct, so that
	// the Katanga side can switch to grey screen and stop any use of the old share. 
	if (gGameSharedHandle != NULL)
	{
		LOG(INFO) << "vr:CreateSharedTexture rebuild gGameSharedHandle. gGameTexture: " << gGameTexture << " gGameSharedHandle: " << gGameSharedHandle;

		// Save possible prior usage to be disposed after we recreate.
		oldGameTexture = gGameTexture;

		// Tell Katanga it's gone, so it can drop its buffers.
		gGameSharedHandle = NULL;
		*(PUINT)(gMappedView) = PtrToUint(gGameSharedHandle);

		return;
	}


	// Now that we have a proper texture from the rehade copy, let's also make a 
	// DX11 Texture2D exact copy, so that we can snapshot the game output. 

	doubleTex->GetDesc(&desc);

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

	// This texture needs to use the Shared flag, so that we can share it to 
	// another Device.  Because these are all DX11 objects, the share will work.

// For half SBS, just single width.  Half for each eye.
	//desc.Width *= 2;								// Double width texture for stereo.
	desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;	// Must add bind flag, so SRV can be created in Unity.
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;	// To be shared. maybe D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX is better
													// But we never seem to see any contention between game and Katanga.
	LOG(INFO) << "  Width: " << desc.Width << ", Height: " << desc.Height << ", Format: " << desc.Format;

	doubleTex->GetDevice(&pDevice);
	if (pDevice == nullptr) FatalExit(L"Failed to GetDevice", 0);
	{
		hr = pDevice->CreateTexture2D(&desc, NULL, &gGameTexture);
		if (FAILED(hr)) FatalExit(L"Fail to create shared stereo Texture", hr);
	}
	pDevice->Release();

	LOG(INFO) << " pDevice create new gGameTexture: " << gGameTexture;

	// Now create the HANDLE which is used to share surfaces.  This follows the model from:
	// https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/nf-d3d11-id3d11device-opensharedresource

	IDXGIResource* pDXGIResource = NULL;

	hr = gGameTexture->QueryInterface(__uuidof(IDXGIResource), (LPVOID*)&pDXGIResource);
	if (FAILED(hr))	FatalExit(L"Fail to QueryInterface on shared surface", hr);
	{
		LOG(INFO) << " query new pDXGIResource: " << pDXGIResource;

		hr = pDXGIResource->GetSharedHandle(&gGameSharedHandle);
		if (FAILED(hr) || gGameSharedHandle == NULL) FatalExit(L"Fail to pDXGIResource->GetSharedHandle", hr);

		LOG(INFO) << " GetSharedHandle new gGameSharedHandle: " << gGameSharedHandle;
	}
	pDXGIResource->Release();

	LOG(INFO) << "  Successfully created new shared gGameTexture: " << gGameTexture << ", new shared gGameSharedHandle: " << gGameSharedHandle;

	// Move that shared handle into the MappedView to IPC the Handle to Katanga.
	// The HANDLE is always 32 bit, even for 64 bit processes.
	// https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication
	//
	// This magic line of code will fire off the Katanga side rebuilding of it's drawing pipeline and surface.

	*(PUINT)(gMappedView) = PtrToUint(gGameSharedHandle);

	LOG(INFO) << "  Successfully shared gMappedView: " << gMappedView << "->" << *(UINT*)(gMappedView);

	// If we already had created one, let the old one go.  We do it after the recreation
	// here fills in the prior globals, to avoid possible dead structure usage in the
	// Unity app.

	LOG(INFO) << "  Release stale gGameTexture: " << oldGameTexture;
	if (oldGameTexture)
		oldGameTexture->Release();
}

// -----------------------------------------------------------------------------

// Move the image halfway, so that we can see half of each eye on the main view.
// This is just a hack way to be sure we are getting stereo output in debug builds.

#ifdef _DEBUG
void DrawStereoOnGame(ID3D11DeviceContext* pContext, ID3D11Texture2D* surface, ID3D11Texture2D* back,
	UINT width, UINT height)
{
	D3D11_BOX srcBox = { width / 2, 0, 0, width + width / 2, height, 1 };
	pContext->CopySubresourceRegion(back, 0, 0, 0, 0, surface, 0, &srcBox);
}
#endif


// Capture the double width texture and transfer the data across the MappedFileIPC to the VR app.
// We do the initialization check here at every frame so we can use a late-binding 
// approach for the sharing of the data, which is more reliable.

void vr::CaptureVRFrame(IDXGISwapChain* swapchain, ID3D11Texture2D* doubleTex)
{
	D3D11_TEXTURE2D_DESC pDesc;
	ID3D11Device* pDevice = nullptr;
	ID3D11DeviceContext* pContext = nullptr;

	// Create the shared texture at first Present, or whenever the gGameSharedHandle is
	// zeroed out as part of a ResizeBuffers.
	if (gGameSharedHandle == NULL)
		CreateSharedTexture(doubleTex);

	// Copy the current data from doubleTex texture into our shared texture every frame.
	if (doubleTex != nullptr && gGameTexture != nullptr)
	{
		doubleTex->GetDesc(&pDesc);
		doubleTex->GetDevice(&pDevice);
		pDevice->GetImmediateContext(&pContext);

//		CaptureSetupMutex();
		{
			D3D11_BOX rightEye = { pDesc.Width / 2, 0, 0, pDesc.Width, pDesc.Height, 1 };
			D3D11_BOX leftEye = { 0, 0, 0, pDesc.Width / 2, pDesc.Height, 1 };

			// SBS needs eye swap to match 3D Vision R/L cross-eyed format of Katanga
			pContext->CopySubresourceRegion(gGameTexture, 0, 0, 0, 0, doubleTex, 0, &rightEye);
			pContext->CopySubresourceRegion(gGameTexture, 0, pDesc.Width / 2, 0, 0, doubleTex, 0, &leftEye);

			//hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));

			//NvAPI_Status status = NvAPI_Stereo_SetActiveEye(_runtime_d3d11._stereo_handle, NVAPI_STEREO_EYE_LEFT);
			//if (SUCCEEDED(status))
			//	pContext->CopySubresourceRegion(gGameTexture, 0, pDesc.Width / 2, 0, 0, doubleTex, 0, &leftEye);
			//NvAPI_Status status = NvAPI_Stereo_SetActiveEye(proxy_device->_stereo_handle, NVAPI_STEREO_EYE_RIGHT);
			//if (SUCCEEDED(status))
			//	pContext->CopySubresourceRegion(gGameTexture, 0, pDesc.Width / 2, 0, 0, doubleTex, 0, &leftEye);
		}
//		ReleaseSetupMutex();


#ifdef _DEBUG
		DrawStereoOnGame(pContext, gGameTexture, doubleTex, pDesc.Width, pDesc.Height);
#endif
		pContext->Release();
		pDevice->Release();
	}
}

