#pragma once

#include <dxgi1_2.h>
#include <d3d12.h>

#include "vr.hpp"


class vr_d3d12 : public vr
{
	vr_d3d12();
	~vr_d3d12();

	void CaptureVRFrame(IDXGISwapChain* swapchain, ID3D12Resource* doubleTex);
};
