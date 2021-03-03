#pragma once

#include <dxgi1_5.h>
#include <d3d12.h>

#include "vr.hpp"


class vr_d3d12 : public vr
{
public:
	vr_d3d12(IDXGISwapChain3 *swapchain);
	~vr_d3d12();

	void CaptureVRFrame(ID3D12Resource* doubleTex);

private:
	IDXGISwapChain3 *_swapchain;
};
