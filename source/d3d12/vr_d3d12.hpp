#pragma once

#include <dxgi1_5.h>
#include <d3d12.h>

#include "vr.hpp"


class vr_d3d12 : public vr
{
public:
	vr_d3d12(IDXGISwapChain3 *swapchain);
	~vr_d3d12();

	void CreateSharedTexture(IUnknown * gameDoubleTex);
	void CaptureVRFrame(ID3D12Resource* pDoubleTex, ID3D12GraphicsCommandList* pCmdList);

private:
	IDXGISwapChain3 *_swapchain;
};
