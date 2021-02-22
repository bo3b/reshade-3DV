#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>

#include "runtime.hpp"

class vr
{
public:
	vr();
	~vr();

	void CreateFileMappedIPC();
	void ReleaseFileMappedIPC();

	void CreateCaptureMutex();
	void DisposeCaptureMutex();
	void CaptureSetupMutex();
	void ReleaseSetupMutex();

	void CreateSharedTexture(ID3D11Texture2D* doubleTex);
	void DestroySharedTexture();
	void CaptureVRFrame(IDXGISwapChain* swapchain, ID3D11Texture2D* doubleTex);

private:
	// For the IPC between the game here with Reshade enabled, and the Katanga VR.
	HANDLE gMappedFile = NULL;
	LPVOID gMappedView = nullptr;

	// The surface that we copy the current stereo game frame into. It is shared.
	// It starts as a Texture so that it is created stereo, and is shared 
	// via file mapped IPC.
	ID3D11Texture2D* gGameTexture = nullptr;

	// The gGameSharedHandle is set always a 32 bit value, not a pointer.  Even for
	// x64 games, because Windows maps these.  Unity side will always use x32 value.
	// The gGameSharedHandle is the shared reference to the actual gGameTexture.
	HANDLE gGameSharedHandle = NULL;

	// The Named Mutex to prevent the VR side from interfering with game side, during
	// the creation or reset of the graphic device.  
	HANDLE gSetupMutex = NULL;
};

