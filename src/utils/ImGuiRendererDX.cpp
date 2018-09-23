#include "stdafx.h"
#include "ImGuiRendererDX.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

/*
IMGUI_IMPL_API bool     ImGui_ImplDX12_Init(ID3D12Device* device, int num_frames_in_flight, DXGI_FORMAT rtv_format,
                                            D3D12_CPU_DESCRIPTOR_HANDLE font_srv_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE font_srv_gpu_desc_handle);
IMGUI_IMPL_API void     ImGui_ImplDX12_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplDX12_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplDX12_RenderDrawData(ImDrawData* draw_data, ID3D12GraphicsCommandList* graphics_command_list);

IMGUI_IMPL_API bool     ImGui_ImplWin32_Init(void* hwnd);
IMGUI_IMPL_API void     ImGui_ImplWin32_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplWin32_NewFrame();
*/

void ImGui::RendererDX::Initialize(HWND hwnd, ID3D12Device *device, DXGI_FORMAT rtvFormat, std::function<std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>(void)> allocator)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImFont* font = io.Fonts->AddFontFromFileTTF("..\\assets\\segoeui.ttf", 18.0f);
    IM_ASSERT(font != NULL);

    ImGui::StyleColorsDark();

    D3D12_CPU_DESCRIPTOR_HANDLE fontSrvDescCpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE fontSrvDescGpuHandle;
    std::tie(fontSrvDescCpuHandle, fontSrvDescGpuHandle) = allocator();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(device, 3, rtvFormat, fontSrvDescCpuHandle, fontSrvDescGpuHandle);
}

void ImGui::RendererDX::Shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ImGui::RendererDX::NewFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGui::RendererDX::Render(ID3D12GraphicsCommandList *commandList)
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT ImGui::RendererDX::WindowProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}
