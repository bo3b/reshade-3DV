#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>

#include "runtime_d3d11.hpp"
#include "d3d11_device.hpp"

void CaptureVRFrame(IDXGISwapChain* swapchain, ID3D11Texture2D* doubleTex);
void DestroySharedTexture();

void Nv3DDirectSetup();
void NvCreateStereoHandle(D3D11Device* device_proxy);
