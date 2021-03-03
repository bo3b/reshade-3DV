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


vr_d3d11::vr_d3d11(IDXGISwapChain* swapchain)
{
	_swapchain = swapchain;
}
vr_d3d11::~vr_d3d11()
{
}

// -----------------------------------------------------------------------------


// Capture the double width texture and transfer the data across the MappedFileIPC to the VR app.
// We do the initialization check here at every frame so we can use a late-binding 
// approach for the sharing of the data, which is more reliable.

void vr_d3d11::CaptureVRFrame(ID3D11Texture2D* doubleTex)
{
	D3D11_TEXTURE2D_DESC pDesc;
	ID3D11Device* pDevice = nullptr;
	ID3D11DeviceContext* pContext = nullptr;

	if (doubleTex == nullptr)
		return;

	doubleTex->GetDesc(&pDesc);
	doubleTex->GetDevice(&pDevice);
	pDevice->GetImmediateContext(&pContext);

	// Create the shared texture at first Present, or whenever the gGameSharedHandle is
	// zeroed out as part of a ResizeBuffers.
	if (gGameSharedHandle == NULL)
	{
		HRESULT hr = pDevice->CreateTexture2D(&pDesc, NULL, &gGameTexture);
		if (FAILED(hr)) FatalExit(L"Fail to create shared stereo Texture", hr);

		CreateSharedTexture(gGameTexture);
	}

	// Copy the current data from doubleTex texture into our shared texture every frame.
	if (doubleTex != nullptr && gGameTexture != nullptr)
	{
//		CaptureSetupMutex();
		{
			D3D11_BOX rightEye = { pDesc.Width / 2, 0, 0, pDesc.Width, pDesc.Height, 1 };
			D3D11_BOX leftEye = { 0, 0, 0, pDesc.Width / 2, pDesc.Height, 1 };

			// SBS needs eye swap to match 3D Vision R/L cross-eyed format of Katanga
			pContext->CopySubresourceRegion(gGameTexture, 0, 0, 0, 0, doubleTex, 0, &rightEye);
			pContext->CopySubresourceRegion(gGameTexture, 0, pDesc.Width / 2, 0, 0, doubleTex, 0, &leftEye);
		}
//		ReleaseSetupMutex();


#ifdef _DEBUG
		DrawStereoOnGame(pContext, gGameTexture, doubleTex, pDesc.Width, pDesc.Height);
#endif
		pContext->Release();
		pDevice->Release();
	}
}

