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

#include "vr_d3d12.hpp"

#include "dll_log.hpp"


vr_d3d12::vr_d3d12(IDXGISwapChain3 *swapchain)
{
	_swapchain = swapchain;
	CreateFileMappedIPC();
}
vr_d3d12::~vr_d3d12()
{
	ReleaseFileMappedIPC();
}

// -----------------------------------------------------------------------------


// Capture the double width texture and transfer the data across the MappedFileIPC to the VR app.
// We do the initialization check here at every frame so we can use a late-binding 
// approach for the sharing of the data, which is more reliable.

void vr_d3d12::CaptureVRFrame(ID3D12Resource* doubleTex)
{
	D3D11_TEXTURE2D_DESC pDesc;
	ID3D11Device* pDevice = nullptr;
	ID3D12GraphicsCommandList* pCmdList = nullptr;

	// Create the shared texture at first Present, or whenever the gGameSharedHandle is
	// zeroed out as part of a ResizeBuffers.
	//if (gGameSharedHandle == NULL)
	//	CreateSharedTexture(doubleTex);

	// Copy the current data from doubleTex texture into our shared texture every frame.
	if (doubleTex != nullptr && gGameTexture != nullptr)
	{
//		doubleTex->GetDesc(&pDesc);
//		doubleTex->GetDevice(&pDevice);
//		pDevice->GetImmediateContext(&pCmdList);
//
////		CaptureSetupMutex();
		{
//			D3D12_BOX rightEye = { pDesc.Width / 2, 0, 0, pDesc.Width, pDesc.Height, 1 };
//			D3D12_BOX leftEye = { 0, 0, 0, pDesc.Width / 2, pDesc.Height, 1 };
//
//			CD3DX12_TEXTURE_COPY_LOCATION Dst(pDestinationResource, i + FirstSubresource);
//			CD3DX12_TEXTURE_COPY_LOCATION source(pIntermediate, pLayouts[i]);
//
//			// SBS needs eye swap to match 3D Vision R/L cross-eyed format of Katanga
//			pCmdList->CopyTextureRegion(gGameTexture, 0, 0, 0, doubleTex, &rightEye);
//			pCmdList->CopyTextureRegion(gGameTexture, pDesc.Width / 2, 0, 0, doubleTex, &leftEye);
		}
//		ReleaseSetupMutex();

		pCmdList->Release();
		pDevice->Release();
	}
}

