#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>

#include "vr.hpp"


class vr_d3d11 : public vr
{
	vr_d3d11();
	~vr_d3d11();

	void CaptureVRFrame(IDXGISwapChain* swapchain, ID3D11Texture2D* doubleTex);
};
