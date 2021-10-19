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
#include "dxgi/format_utils.hpp"


vr_d3d12::vr_d3d12(IDXGISwapChain3 *swapchain)
{
	_swapchain = swapchain;
	CreateFileMappedIPC();
}
vr_d3d12::~vr_d3d12()
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
// specific DX12 case, so that we can copy the DX12 backbuffer into this DX12
// shared texture.
//
// This will also rewrite the global _game_sharedhandle with a new HANDLE as
// needed, and the Unity side is expected to notice a change and setup a new
// drawing texture as well.  This is thus polling on the Unity side, which is
// not ideal, but it is one call here to fetch the 4 byte HANDLE every 11ms.  
// There does not appear to be a good way for this code to notify the C# code,
// although using a TriggerEvent with some C# interop might work.  We'll only
// do that work if this proves to be a problem.

void vr_d3d12::CreateSharedTexture(IUnknown* gameDoubleTex)
{
	vr::CreateSharedTexture(gameDoubleTex);

	//res_desc = reinterpret_cast<ID3D12Resource*>(gameDoubleTex)->GetDesc();
	//reinterpret_cast<ID3D12Resource*>(gameDoubleTex)->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&pDevice));



	//// Create destination texture for sharing. Same Desc as the input DoubleTex.
	//// This will be a 2x buffer.
	//// https://stackoverflow.com/questions/52869111/sharing-id3d11buffer-and-id3d12resource

	//ID3D12Resource* share;
	//D3D12_HEAP_PROPERTIES heap_props = { D3D12_HEAP_TYPE_DEFAULT };
	//D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_SHARED;
	//res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	//D3D12_RESOURCE_STATES res_states = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	//D3D12_CLEAR_VALUE* clear_value = nullptr;

	//hr = pDevice->CreateCommittedResource(&heap_props, heap_flags, &res_desc, res_states, clear_value,
	//	IID_PPV_ARGS(&share));
	//if (FAILED(hr)) FatalExit(L"Fail to create shared stereo Texture", hr);

	//share->SetName(L"Shared double-width stereo texture");

	//// Now create the HANDLE which is used to share surfaces. 
	//hr = pDevice->CreateSharedHandle(share, nullptr, GENERIC_ALL, L"stereo_share", &_game_sharedhandle);
	//if (FAILED(hr) || _game_sharedhandle == NULL) FatalExit(L"Fail to pDevice->CreateSharedHandle", hr);

	//_shared_texture = share;
	//pDevice->Release();

	//LOG(INFO) << "  Successfully created new DX12 _shared_texture: " << _shared_texture << ", new shared _game_sharedhandle: " << _game_sharedhandle;

	//// Move that shared handle into the MappedView to IPC the Handle to Katanga.
	//// The HANDLE is always 32 bit, even for 64 bit processes.
	//// https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication
	////
	//// This magic line of code will fire off the Katanga side rebuilding of it's drawing pipeline and surface.

	//*(PUINT)(_mapped_view) = PtrToUint(_game_sharedhandle);

	//// If we already had created one, let the old one go.  We do it after the recreation
	//// here fills in the prior globals, to avoid possible dead structure usage in the
	//// Unity app.

	//LOG(INFO) << "  Release stale _shared_texture: " << oldGameTexture;
	//if (oldGameTexture)
	//	oldGameTexture->Release();
}

// -----------------------------------------------------------------------------

// Capture the double width texture and transfer the data across the MappedFileIPC to the VR app.
// We do the initialization check here at every frame so we can use a late-binding 
// approach for the sharing of the data, which is more reliable.

void vr_d3d12::CaptureVRFrame(ID3D12Resource* pDoubleTex, ID3D12GraphicsCommandList* pCmdList)
{
	D3D12_RESOURCE_DESC desc;

	// Create the shared texture at first Present, or whenever the _game_sharedhandle is
	// zeroed out as part of a ResizeBuffers.
	if (_game_sharedhandle == NULL)
		CreateSharedTexture(pDoubleTex);

	// Copy the current data from pDoubleTex texture into our shared texture every frame.
	if (pDoubleTex != nullptr && _dx12_shared_resource != nullptr)
	{
		desc = pDoubleTex->GetDesc();

		D3D12_BOX rightEye = { desc.Width / 2, 0, 0, desc.Width, desc.Height, 1 };
		D3D12_BOX leftEye = { 0, 0, 0, desc.Width / 2, desc.Height, 1 };

		D3D12_TEXTURE_COPY_LOCATION doubleTex_source = { pDoubleTex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };
		D3D12_TEXTURE_COPY_LOCATION shared_dest = { reinterpret_cast<ID3D12Resource*>(_dx12_shared_resource), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };

		// full SBS needs eye swap to match 3D Vision R/L cross-eyed format for Katanga
		pCmdList->CopyTextureRegion(&shared_dest, 0, 0, 0, &doubleTex_source, &rightEye);
		pCmdList->CopyTextureRegion(&shared_dest, desc.Width / 2, 0, 0, &doubleTex_source, &leftEye);
	}
}

