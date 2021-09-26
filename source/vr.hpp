#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>

#include "runtime.hpp"
#include "d3d11/nvapi/nvapi_lite_stereo.h"

void Nv3DDirectSetup();

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

	virtual void CreateSharedTexture(IUnknown* gameTexture) = 0;
	void DestroySharedTexture();

	DECLSPEC_NORETURN void DoubleBeepExit();
	void FatalExit(LPCWSTR errorString, HRESULT code);

protected:
	// For the IPC between the game here with Reshade enabled, and the Katanga VR.
	HANDLE _mapped_file = NULL;
	LPVOID _mapped_view = nullptr;

	// The surface that we copy the current stereo game frame into. It is shared.
	// It starts as a Texture so that it is created stereo, and is shared 
	// via file mapped IPC. This is IUnknown because we need to create shared surfaces
	// from each API.  On the Katanga side, it will just see the share.
	IUnknown* _shared_texture = nullptr;

	// The _game_sharedhandle is set always a 32 bit value, not a pointer.  Even for
	// x64 games, because Windows maps these.  Unity side will always use x32 value.
	// The _game_sharedhandle is the shared reference to the actual _shared_texture.
	HANDLE _game_sharedhandle = NULL;

	// The Named Mutex to prevent the VR side from interfering with game side, during
	// the creation or reset of the graphic device.  
	HANDLE _setup_mutex = NULL;
};

