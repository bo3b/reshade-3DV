// This code is for the VR connection to HelixVision.
//
// It's not set up as a C++ object, because the wrapper objects can only be one
// level deep because of the COM connection. So subclasses don't work, because the
// vtable doesn't match.  Also, we need to call out to CaptureVRFrame from Present,
// and a subclass would not have the right location without duplicating code.
// Setting this up as static routines with file specific variables seems simplest.
//
// Keeping this in a separate file, as opposed to directly modifying HackerDXGI, 
// because we might need to merge in future 3Dmigoto changes, and keeping the changes
// minimized there will improve the compatibility.  And keeps all this VR and IPC
// stuff in one clear spot.
//
// Copied from 3Dmigoto version with HelixVision, but modified to remove all nvidia
// 3D Vision requirements.  Backbuffer will be a SBS image.

#include "vr_d3d11.hpp"

#include "dll_log.hpp"
#include "nvapi/nvapi.h"
#include "dxgi/format_utils.hpp"


// -----------------------------------------------------------------------------

// If 3D Vision is enabled, we'll take that as the signal to setup for Direct Mode
// output to 3D Vision, of the DoubleTex buffer.  That will allow us to handle SBS
// cases like ROTTR, as well as convert SuperDepth3D into 3D Vision.
//
// If we have no NVidia driver, or no 3D enabled, we won't return a proper StereoHandle,
// and thus will skip calling any NVidia calls later on.
//
// This Nv3DDirectSetup must be called before the device is created in order to set up
// DirectMode.

void Nv3DDirectSetup()
{
	NvAPI_Status status;

	status = NvAPI_Initialize();
	if (status != NVAPI_OK)
	{
		LOG(INFO) << " D3D11CreateDevice - NvAPI_Initialize" << " failed with error code " << status << '.';
		return;
	}

	status = NvAPI_Stereo_SetDriverMode(NVAPI_STEREO_DRIVER_MODE_DIRECT);
	if (status != NVAPI_OK)
	{
		LOG(INFO) << " D3D11CreateDevice - NvAPI_Stereo_SetDriverMode" << " failed with error code " << status << '.';
		return;
	}
}

// This NvCreateStereoHandle can only be called after the device is created.

void vr_d3d11::NvCreateStereoHandle(IDXGISwapChain* swapchain)
{
	NvAPI_Status status;
	HRESULT hr;
	ID3D11Device* pDevice;
	
	hr = swapchain->GetDevice(__uuidof(pDevice), reinterpret_cast<void**>(&pDevice));
	{
		if (FAILED(hr))
		{
			LOG(INFO) << " D3D11CreateDevice - swapchain->GetDevice failed: " << status << ". 3D Vision will be disabled.";
			return;
		}

		status = NvAPI_Stereo_CreateHandleFromIUnknown(pDevice, &_stereohandle);
		if (FAILED(status))
		{
			LOG(INFO) << " D3D11CreateDevice - NvAPI_Stereo_CreateHandleFromIUnknown failed: " << status << ". 3D Vision will be disabled.";
			return;
		}
	}
	pDevice->Release();

	LOG(INFO) << "D3D11CreateDevice - NvAPI_Stereo_CreateHandleFromIUnknown successfully created " << status << " stereohandle: " << _stereohandle;
	LOG(INFO) << "> 3D Vision will be active.";
}

// ----------------------------------------------------------------------

vr_d3d11::vr_d3d11(IDXGISwapChain* swapchain)
{
	_swapchain = swapchain;
	CreateFileMappedIPC();
	NvCreateStereoHandle(swapchain);
}
vr_d3d11::~vr_d3d11()
{
	ReleaseFileMappedIPC();
}

// ----------------------------------------------------------------------
// For when we need to create the offscreen Texture2D for our stereo
// copy.  This is created when the CreateSwapChain is called, and also whenever
// ResizeBuffers is called, because we need to change our destination copy
// to always match what the game is drawing. Buffer width is already 2x game size,
// as it comes from SuperDepth doubleTex.
//
// This is the override of the virtual function, which is done here for the
// specific DX11 case, so that we can copy the DX11 backbuffer into this DX11
// shared texture.
//
// This will also rewrite the global _game_sharedhandle with a new HANDLE as
// needed, and the Unity side is expected to notice a change and setup a new
// drawing texture as well.  This is thus polling on the Unity side, which is
// not ideal, but it is one call here to fetch the 4 byte HANDLE every 11ms.  
// There does not appear to be a good way for this code to notify the C# code,
// although using a TriggerEvent with some C# interop might work.  We'll only
// do that work if this proves to be a problem.

void vr_d3d11::CreateSharedTexture(IUnknown* gameDoubleTex)
{
	HRESULT hr;
	D3D11_TEXTURE2D_DESC desc = { 0 };
	IUnknown* oldGameTexture = _shared_texture;
	ID3D11Device* pDevice = nullptr;

	reinterpret_cast<ID3D11Texture2D*>(gameDoubleTex)->GetDesc(&desc);
	reinterpret_cast<ID3D11Texture2D*>(gameDoubleTex)->GetDevice(&pDevice);

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

	hr = pDevice->CreateTexture2D(&desc, NULL, reinterpret_cast<ID3D11Texture2D**>(&_shared_texture));
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

	LOG(INFO) << "  Successfully created new DX11 _shared_texture: " << _shared_texture << ", new shared _game_sharedhandle: " << _game_sharedhandle;

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

// ----------------------------------------------------------------------

// Capture the double width texture and transfer the data across the MappedFileIPC to the VR app.
// We do the initialization check here at every frame so we can use a late-binding 
// approach for the sharing of the data, which is more reliable.  This is called for every
// Present call.

void vr_d3d11::CaptureVRFrame(ID3D11Texture2D* doubleTex)
{
	D3D11_TEXTURE2D_DESC desc;
	ID3D11Device* pDevice = nullptr;
	ID3D11DeviceContext* pContext = nullptr;
	ID3D11Texture2D* backBuffer = nullptr;
	NvAPI_Status status;
	HRESULT hr;

	// Create the shared texture at first Present, or whenever the _game_sharedhandle is
	// zeroed out as part of a ResizeBuffers.
	if (_game_sharedhandle == NULL)
		CreateSharedTexture(doubleTex);

	// Copy the current frame data from doubleTex texture into our shared texture.
	if (doubleTex != nullptr && _shared_texture != nullptr)
	{
		doubleTex->GetDesc(&desc);
		doubleTex->GetDevice(&pDevice);
		pDevice->GetImmediateContext(&pContext);
		{
			D3D11_BOX rightEye = { desc.Width / 2, 0, 0, desc.Width, desc.Height, 1 };
			D3D11_BOX leftEye = { 0, 0, 0, desc.Width / 2, desc.Height, 1 };

			// SBS needs eye swap to match 3D Vision R/L cross-eyed format of Katanga
			pContext->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(_shared_texture), 0, 0, 0, 0, doubleTex, 0, &rightEye);
			pContext->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(_shared_texture), 0, desc.Width / 2, 0, 0, doubleTex, 0, &leftEye);

			// Only do the 3D Vision part if 3D Vision was successfully enabled, indicated by
			// a valid _stereohandle.
			if (_stereohandle != NULL)
			{
				hr = _swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
				if (SUCCEEDED(hr) && backBuffer != nullptr)
				{
					status = NvAPI_Stereo_SetActiveEye(_stereohandle, NVAPI_STEREO_EYE_LEFT);
					if (SUCCEEDED(status))
						pContext->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, doubleTex, 0, &leftEye);
					else
						LOG(WARN) << "  Failed SetActiveEye leftEye: " << status; // << " for rect: " << leftEye;

					status = NvAPI_Stereo_SetActiveEye(_stereohandle, NVAPI_STEREO_EYE_RIGHT);
					if (SUCCEEDED(status))
						pContext->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, doubleTex, 0, &rightEye);
					else
						LOG(WARN) << "  Failed SetActiveEye rightEye: " << status; // << " for rect: " << rightEye;

					backBuffer->Release();
				}
			}

			pContext->Release();
			pDevice->Release();
		}
	}
}

