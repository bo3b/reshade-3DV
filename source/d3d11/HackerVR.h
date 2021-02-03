#pragma once

#include <dxgi1_2.h>
#include <d3d11_1.h>

#include "runtime_d3d11.hpp"

void CaptureVRFrame(reshade::d3d11::runtime_d3d11* _runtime_d3d11, ID3D11Texture2D* doubleTex);
void DestroySharedTexture();

