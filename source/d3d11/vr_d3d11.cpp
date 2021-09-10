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
			pContext->CopySubresourceRegion(_shared_texture, 0, 0, 0, 0, doubleTex, 0, &rightEye);
			pContext->CopySubresourceRegion(_shared_texture, 0, desc.Width / 2, 0, 0, doubleTex, 0, &leftEye);

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

