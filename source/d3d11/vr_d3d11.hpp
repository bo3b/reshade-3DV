#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>

#include "vr.hpp"


class vr_d3d11 : public vr
{
public:
	vr_d3d11(IDXGISwapChain* swapchain);
	~vr_d3d11();

	void CaptureVRFrame(ID3D11Texture2D* doubleTex);

private:
	IDXGISwapChain* _swapchain;
};
