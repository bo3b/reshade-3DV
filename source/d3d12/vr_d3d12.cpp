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

#include "d3d12.h"
#include "d3dx12.h""

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

// Get the shared surface specified by the input HANDLE.  This will be from 
// the game side, in DX9Ex. This technique is specified in the documentation:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ff476531(v=vs.85).aspx
// https://msdn.microsoft.com/en-us/library/windows/desktop/ee913554(v=vs.85).aspx

// For the shared surface itself, disposed when recreated.
ID3D11Texture2D* pTexture2D = nullptr;
ID3D11ShaderResourceView* pSRView = nullptr;

HANDLE CreateDX11SharedSurface(ID3D11Device* pDevice, IUnknown* gameDoubleTex)
{
	HRESULT hr;
	D3D11_TEXTURE2D_DESC desc = { 0 };
	//IUnknown* oldGameTexture = _shared_texture;
	//ID3D11Device* pDevice = nullptr;

	//reinterpret_cast<ID3D11Texture2D*>(gameDoubleTex)->GetDesc(&desc);
	//reinterpret_cast<ID3D11Texture2D*>(gameDoubleTex)->GetDevice(&pDevice);

	//LOG(INFO) << "vr::CreateSharedTexture called. _shared_texture: " << _shared_texture << " _game_sharedhandle: " << _game_sharedhandle << " _mapped_view: " << _mapped_view;

	//LOG(INFO) << "  | DoubleTex                               |                                         |";
	//LOG(INFO) << "  +-----------------------------------------+-----------------------------------------+";
	//LOG(INFO) << "  | Width                                   | " << std::setw(39) << desc.Width << " |";
	//LOG(INFO) << "  | Height                                  | " << std::setw(39) << desc.Height << " |";
	//if (const char *format_string = format_to_string(desc.Format); format_string != nullptr)
	//	LOG(INFO) << "  | Format                                  | " << std::setw(39) << format_string << " |";
	//else
	//	LOG(INFO) << "  | Format                                  | " << std::setw(39) << desc.Format << " |";
	//LOG(INFO) << "  +-----------------------------------------+-----------------------------------------+";



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


	//HRESULT hr;

	//// Even though the input shared surface is a RenderTarget Surface, this
	//// Query for Texture2D still works.  Not sure if it is good or bad.
	//ID3D11Device1* pDevice1 = nullptr;
	//hr = pDevice->QueryInterface(__uuidof(ID3D11Device1), (void**)&pDevice1);
	//{
	//	if (FAILED(hr) || (pDevice1 == nullptr)) DebugBreak(); // FatalExit(L"Failed to upcast ID3D11Device->ID3D11Device1.", hr);

	//	hr = pDevice1->OpenSharedResource1(shared, __uuidof(ID3D11Texture2D), (void**)(&pTexture2D));
	//	LOG(INFO) << "....OpenSharedResource on shared: " << shared  << ", result: " << hr << ", resource: " << pTexture2D;

	//	if (FAILED(hr) || (pTexture2D == nullptr)) DebugBreak(); // FatalExit(L"Failed to open shared surface.", hr);
	//}
	//pDevice1->Release();

	// By capturing the Width/Height/Format here, we can let Unity side
	// know what buffer to build to match.
	//D3D11_TEXTURE2D_DESC tdesc;
	//UINT gWidth;
	//UINT gHeight;
	//DXGI_FORMAT gFormat;
	//pTexture2D->GetDesc(&tdesc);
	//gWidth = tdesc.Width;
	//gHeight = tdesc.Height;
	//gFormat = tdesc.Format;

	//LOG(INFO) << "....Successful GetDesc on surface - Width: %d, Height: %d, Format: %d\n" << 
	//	tdesc.Width << tdesc.Height << tdesc.Format;

	//// This is theoretically the exact same surface in the video card memory,
	//// that the game's DX11 is using as the stereo shared surface. 
	////
	//// Now we need to create a ShaderResourceView using this, because that
	//// is what Unity requires for its CreateExternalTexture.
	////
	//// No need to change description, we want it to be the same as what the game
	//// specifies, so passing NULL to make it identical.

	//hr = pDevice->CreateShaderResourceView(pTexture2D, NULL, &pSRView);
	//LOG(INFO) << "....CreateShaderResourceView on texture: %p, result: %d, SRView: %p\n" <<  pTexture2D <<  hr << pSRView;
	//if (FAILED(hr))	DebugBreak(); // FatalExit(L"Failed to CreateShaderResourceView.", hr);

	//return pSRView;
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
	HRESULT hr;
	D3D12_RESOURCE_DESC res_desc {};
	IUnknown* oldGameTexture = _shared_texture;
	ID3D12Device* pDevice = nullptr;

	res_desc = reinterpret_cast<ID3D12Resource*>(gameDoubleTex)->GetDesc();
	reinterpret_cast<ID3D12Resource*>(gameDoubleTex)->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&pDevice));

	LOG(INFO) << "vr::CreateSharedTexture called. _shared_texture: " << _shared_texture << " _game_sharedhandle: " << _game_sharedhandle << " _mapped_view: " << _mapped_view;

	LOG(INFO) << "  | DoubleTex                               |                                         |";
	LOG(INFO) << "  +-----------------------------------------+-----------------------------------------+";
	LOG(INFO) << "  | Width                                   | " << std::setw(39) << res_desc.Width << " |";
	LOG(INFO) << "  | Height                                  | " << std::setw(39) << res_desc.Height << " |";
	if (const char *format_string = format_to_string(res_desc.Format); format_string != nullptr)
		LOG(INFO) << "  | Format                                  | " << std::setw(39) << format_string << " |";
	else
		LOG(INFO) << "  | Format                                  | " << std::setw(39) << res_desc.Format << " |";
	LOG(INFO) << "  +-----------------------------------------+-----------------------------------------+";

	// Some games like TheSurge and Dishonored2 will specify a DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
// as their backbuffer.  This doesn't work for us because our output is going to the VR HMD,
// and thus we get a doubled up sRGB/gamma curve, which makes it too dark, and the in-game
// slider doesn't have enough range to correct.  
// If we get one of these sRGB formats, we are going to strip that and return the Linear
// version instead, so that we avoid this problem.  This allows us to use Gamma for the Unity
// app itself, which matches 90% of the games, and still handle these oddball games automatically.

	// ToDo: needed for Dx12?
	//if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
	//	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
	//	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

	// Reshade makes all buffers TypeLess, but we need the actual game type when creating a
	// shared surface.

	res_desc.Format = make_dxgi_format_normal(res_desc.Format);

	LOG(INFO) << "  | Final Format                            | " << std::setw(39) << format_to_string(res_desc.Format) << " |";



	// Make a dx11 texture that works, as the surface to be shared, and then fetch reference
	// to it here as DX12

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	ID3D11Device* g_pd3dDevice;
	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
		D3D11_SDK_VERSION, &g_pd3dDevice, nullptr, nullptr);
	if (FAILED(hr)) FatalExit(L"Fail to create text DX11", hr);
	LOG(INFO) << "  Successfully create test dx11 device: " << g_pd3dDevice;

	_game_sharedhandle = CreateDX11SharedSurface(g_pd3dDevice, gameDoubleTex);



	// Create destination texture for sharing. Same Desc as the input DoubleTex.
	// This will be a 2x buffer.
	// https://stackoverflow.com/questions/52869111/sharing-id3d11buffer-and-id3d12resource

//	ID3D12Resource* share;
//	D3D12_HEAP_PROPERTIES heap_props = { D3D12_HEAP_TYPE_DEFAULT };
//	D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_SHARED;
//	res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
//	D3D12_RESOURCE_STATES res_states = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
//	D3D12_CLEAR_VALUE* clear_value = nullptr;
//
//
////	Microsoft::WRL::ComPtr<ID3D12Resource>      m_videoTexture;
//	CD3DX12_RESOURCE_DESC desc(
//		D3D12_RESOURCE_DIMENSION_TEXTURE2D,
//		0,
//		res_desc.Width,
//		res_desc.Height,
//		1,
//		1,
//		DXGI_FORMAT_B8G8R8A8_UNORM,
//		1,
//		0,
//		D3D12_TEXTURE_LAYOUT_UNKNOWN,
//		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
//
//	CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
//
//		hr = pDevice->CreateCommittedResource(
//			&defaultHeapProperties,
//			D3D12_HEAP_FLAG_SHARED,
//			&desc,
//			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
//			nullptr,
//			IID_PPV_ARGS(&share));
//
//	//hr = pDevice->CreateCommittedResource(&heap_props, heap_flags, &res_desc, res_states, clear_value,
//	//	IID_PPV_ARGS(&share));
//	if (FAILED(hr)) FatalExit(L"Fail to create shared stereo Texture", hr);
//
//	share->SetName(L"Shared double-width stereo texture");
//
//	// Now create the HANDLE which is used to share surfaces. 
//	hr = pDevice->CreateSharedHandle(share, nullptr, GENERIC_ALL, L"stereo_share", &_game_sharedhandle);
//	if (FAILED(hr) || _game_sharedhandle == NULL) FatalExit(L"Fail to pDevice->CreateSharedHandle", hr);
//
//	_shared_texture = share;
//	pDevice->Release();
//
//	LOG(INFO) << "  Successfully created new DX12 _shared_texture: " << _shared_texture << ", new shared _game_sharedhandle: " << _game_sharedhandle;
//
//	// Move that shared handle into the MappedView to IPC the Handle to Katanga.
//	// The HANDLE is always 32 bit, even for 64 bit processes.
//	// https://docs.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication
//	//
//	// This magic line of code will fire off the Katanga side rebuilding of it's drawing pipeline and surface.
//
//	*(PUINT)(_mapped_view) = PtrToUint(_game_sharedhandle);
//
//	// If we already had created one, let the old one go.  We do it after the recreation
//	// here fills in the prior globals, to avoid possible dead structure usage in the
//	// Unity app.
//
//	LOG(INFO) << "  Release stale _shared_texture: " << oldGameTexture;
//	if (oldGameTexture)
//		oldGameTexture->Release();

	ID3D12Resource* copy;
	hr = pDevice->OpenSharedHandle(_game_sharedhandle, __uuidof(ID3D12Resource), (LPVOID*)&copy);
	if (FAILED(hr)) FatalExit(L"Fail to open shared stereo Texture", hr);
	LOG(INFO) << "  Successfully opened DX12 _game_sharedhandle: " << copy << ", shared _game_sharedhandle: " << _game_sharedhandle;


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
	if (pDoubleTex != nullptr && _shared_texture != nullptr)
	{
		desc = pDoubleTex->GetDesc();

		D3D12_BOX rightEye = { desc.Width / 2, 0, 0, desc.Width, desc.Height, 1 };
		D3D12_BOX leftEye = { 0, 0, 0, desc.Width / 2, desc.Height, 1 };

		D3D12_TEXTURE_COPY_LOCATION doubleTex_source = { pDoubleTex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };
		D3D12_TEXTURE_COPY_LOCATION shared_dest = { reinterpret_cast<ID3D12Resource*>(_shared_texture), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };

		// full SBS needs eye swap to match 3D Vision R/L cross-eyed format for Katanga
		pCmdList->CopyTextureRegion(&shared_dest, 0, 0, 0, &doubleTex_source, &rightEye);
		pCmdList->CopyTextureRegion(&shared_dest, desc.Width / 2, 0, 0, &doubleTex_source, &leftEye);
	}
}

