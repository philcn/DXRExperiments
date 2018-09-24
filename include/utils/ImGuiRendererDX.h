#pragma once

#include "imgui.h"
#include <functional>

namespace ImGui 
{
    namespace RendererDX
    {
        void Initialize(HWND hwnd, ID3D12Device *device, DXGI_FORMAT rtvFormat, UINT frameCount, std::function<std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>(void)> allocator);
        void Shutdown();
        void NewFrame();
        void Render(ID3D12GraphicsCommandList *commandList);

        LRESULT WindowProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    }
}

namespace ui = ImGui;
