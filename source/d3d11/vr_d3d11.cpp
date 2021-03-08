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


vr_d3d11::vr_d3d11(IDXGISwapChain* swapchain)
{
	_swapchain = swapchain;
	CreateFileMappedIPC();
}
vr_d3d11::~vr_d3d11()
{
	ReleaseFileMappedIPC();
}

// -----------------------------------------------------------------------------

void Nv3DDirectSetup()
{
	NvAPI_Status status = NvAPI_Initialize();
	if (status != NVAPI_OK)
	{
		LOG(WARN) << "D3D11CreateDeviceAndSwapChain SetDriverMode" << " failed with error code " << status << '.';
		return;
	}

	status = NvAPI_Stereo_SetDriverMode(NVAPI_STEREO_DRIVER_MODE_DIRECT);
	if (status != NVAPI_OK)
		LOG(WARN) << "D3D11CreateDeviceAndSwapChain SetDriverMode" << " failed with error code " << status << '.';

	LOG(INFO) << "D3D11CreateDeviceAndSwapChain SetDriverMode" << " successfully set " << status << '.';
}

void NvCreateStereoHandle(D3D11Device* device_proxy)
{
	NvAPI_Status status = NvAPI_Stereo_CreateHandleFromIUnknown(device_proxy->_orig, &gStereoHandle);
	if (FAILED(status))
		LOG(WARN) << "NvAPI_Stereo_CreateHandleFromIUnknown failed: " << status;

	LOG(INFO) << "NvAPI_Stereo_CreateHandleFromIUnknown " << " successfully created " << status << " stereohandle: " << gStereoHandle;
}

// ----------------------------------------------------------------------

// Capture the double width texture and transfer the data across the MappedFileIPC to the VR app.
// We do the initialization check here at every frame so we can use a late-binding 
// approach for the sharing of the data, which is more reliable.  This is called for every
// Present call.

void vr_d3d11::CaptureVRFrame(ID3D11Texture2D* doubleTex)
{
	D3D11_TEXTURE2D_DESC pDesc;
	ID3D11Device* pDevice = nullptr;
	ID3D11DeviceContext* pContext = nullptr;

	// Create the shared texture at first Present, or whenever the _game_sharedhandle is
	// zeroed out as part of a ResizeBuffers.
	if (_game_sharedhandle == NULL)
		CreateSharedTexture(doubleTex);

	// Copy the current frame data from doubleTex texture into our shared texture.
	if (doubleTex != nullptr && _shared_texture != nullptr)
	{
		doubleTex->GetDesc(&pDesc);
		doubleTex->GetDevice(&pDevice);
		pDevice->GetImmediateContext(&pContext);

//		CaptureSetupMutex();
		{
			D3D11_BOX rightEye = { pDesc.Width / 2, 0, 0, pDesc.Width, pDesc.Height, 1 };
			D3D11_BOX leftEye = { 0, 0, 0, pDesc.Width / 2, pDesc.Height, 1 };

			// SBS needs eye swap to match 3D Vision R/L cross-eyed format of Katanga
			pContext->CopySubresourceRegion(_shared_texture, 0, 0, 0, 0, doubleTex, 0, &rightEye);
			pContext->CopySubresourceRegion(_shared_texture, 0, pDesc.Width / 2, 0, 0, doubleTex, 0, &leftEye);
			ID3D11Texture2D* backBuffer = nullptr;
			NvAPI_Status status;
			HRESULT hr;
			hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
			if (SUCCEEDED(hr) && backBuffer != nullptr)
			{
				D3D11_TEXTURE2D_DESC bbDesc;
				backBuffer->GetDesc(&bbDesc);

				status = NvAPI_Stereo_SetActiveEye(gStereoHandle, NVAPI_STEREO_EYE_LEFT);
				if (SUCCEEDED(status))
					pContext->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, doubleTex, 0, &leftEye);
				else
					LOG(INFO) << "  Failed SetActiveEye leftEye: " << status; // << " for rect: " << leftEye;

				status = NvAPI_Stereo_SetActiveEye(gStereoHandle, NVAPI_STEREO_EYE_RIGHT);
				if (SUCCEEDED(status))
					pContext->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, doubleTex, 0, &rightEye);
				else
					LOG(INFO) << "  Failed SetActiveEye rightEye: " << status; // << " for rect: " << rightEye;

#ifdef _DEBUG
				DrawStereoOnGame(pContext, backBuffer, doubleTex, bbDesc.Width, bbDesc.Height);
#endif
				backBuffer->Release();
			}
		}
//		ReleaseSetupMutex();

		pContext->Release();
		pDevice->Release();
	}
}

