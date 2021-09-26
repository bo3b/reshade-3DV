#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>
#include "nvapi/nvapi_lite_stereo.h"

#include "vr.hpp"


class vr_d3d11 : public vr
{
public:
	vr_d3d11(IDXGISwapChain* swapchain);
	~vr_d3d11();

	void CreateSharedTexture(IUnknown* gameDoubleTex);
	void CaptureVRFrame(ID3D11Texture2D* doubleTex);

private:
	void NvCreateStereoHandle(IDXGISwapChain* swapchain);

	IDXGISwapChain* _swapchain = nullptr;
	StereoHandle _stereohandle = NULL;
};
