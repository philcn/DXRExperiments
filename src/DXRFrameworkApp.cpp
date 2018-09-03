//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "DXRFrameworkApp.h"
#include "DirectXRaytracingHelper.h"

#include "nv_helpers_dx12/DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/TopLevelASGenerator.h"

#include "CompiledShaders/Raytracing.hlsl.h"

using namespace std;
using namespace DX;
using namespace DXRFramework;

namespace GlobalRootSignatureParams {
    enum Value {
        AccelerationStructureSlot = 0,
        OutputViewSlot,
        Count 
    };
}

DXRFrameworkApp::DXRFrameworkApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    mRaytracingEnabled(true)
{
    UpdateForSizeChange(width, height);
}

void DXRFrameworkApp::OnInit()
{
    m_deviceResources = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_12_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the Fallback Layer requires Fall Creator's update (RS3), we don't need to handle non-tearing cases.
        DeviceResources::c_RequireTearingSupport,
        m_adapterIDoverride
    );
    m_deviceResources->RegisterDeviceNotify(this);
    m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
    m_deviceResources->InitializeDXGIAdapter();
    mUseDXRDriver = EnableDXRExperimentalFeatures(m_deviceResources->GetAdapter());

    m_deviceResources->CreateDeviceResources();
    m_deviceResources->CreateWindowSizeDependentResources();

    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

void DXRFrameworkApp::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    mRtContext = RtContext::create(device, commandList);

    RtProgram::Desc programDesc;
    programDesc.addShaderLibrary(g_pRaytracing, ARRAYSIZE(g_pRaytracing), {L"RayGen", L"Miss", L"ClosestHit"});
    programDesc.setRayGen("RayGen");
    programDesc.addHitGroup(0, "ClosestHit", "");
    programDesc.addMiss(0, "Miss");
    mRtProgram = RtProgram::create(mRtContext, programDesc);

    mRtState = RtState::create(mRtContext); 
    mRtState->setProgram(mRtProgram);
    mRtState->setMaxTraceRecursionDepth(1);

    mRtBindings = RtBindings::create(mRtContext, mRtProgram);

    CreateGeometries();
    CreateAccelerationStructures();
    // CreateConstantBuffers();
}

void DXRFrameworkApp::CreateWindowSizeDependentResources()
{
    CreateRaytracingOutputBuffer();
    // UpdateCameraMatrices();
}

void DXRFrameworkApp::CreateRaytracingOutputBuffer()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    AllocateUAVTexture(device, backbufferFormat, m_width, m_height, &mOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    UINT outputResourceUAVDescriptorHeapIndex = mRtContext->allocateDescriptor(&uavDescriptorHandle, UINT_MAX);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    mOutputResourceUAVGpuDescriptor = mRtContext->getDescriptorGPUHandle(outputResourceUAVDescriptorHeapIndex);
}

void DXRFrameworkApp::ReleaseWindowSizeDependentResources()
{
    mOutputResource.Reset();
}

void DXRFrameworkApp::ReleaseDeviceDependentResources()
{
    mVertexBuffer.Reset();
    mBlasBuffer.Reset();
    mTlasBuffer.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::CreateGeometries()
{
    Vertex triangleVertices[] =
    {
        { { 0.0f, 0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    auto device = m_deviceResources->GetD3DDevice();
    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be marshalled 
    // over. Please read up on Default Heap usage. An upload heap is used here for 
    // code simplicity and because there are very few verts to actually transfer.
    AllocateUploadBuffer(device, triangleVertices, sizeof(triangleVertices), &mVertexBuffer);
}

AccelerationStructureBuffers DXRFrameworkApp::CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers)
{
    nv_helpers_dx12::BottomLevelASGenerator blasGenerator;

    for (const auto &vb : vertexBuffers) {
        blasGenerator.AddVertexBuffer(vb.first.Get(), 0, vb.second, sizeof(Vertex), nullptr, 0);
    }

    UINT64 scratchSizeInBytes = 0;
    UINT64 resultSizeInBytes = 0;
    blasGenerator.ComputeASBufferSizes(mRtContext->getFallbackDevice(), false, &scratchSizeInBytes, &resultSizeInBytes);

    auto device = m_deviceResources->GetD3DDevice();
    AccelerationStructureBuffers buffers;

    buffers.scratch = nv_helpers_dx12::CreateBuffer(
        device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps);

    D3D12_RESOURCE_STATES initialResourceState = mRtContext->getFallbackDevice()->GetAccelerationStructureResourceState();
    buffers.accelerationStructure = nv_helpers_dx12::CreateBuffer(
        device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        initialResourceState, nv_helpers_dx12::kDefaultHeapProps);

    auto commandList = m_deviceResources->GetCommandList();
    blasGenerator.Generate(commandList, mRtContext->getFallbackCommandList(), buffers.scratch.Get(), buffers.accelerationStructure.Get());

    return buffers;
}

AccelerationStructureBuffers DXRFrameworkApp::CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> &instances)
{
    nv_helpers_dx12::TopLevelASGenerator tlasGenerator;

    for (int i = 0; i < instances.size(); ++i) {
        tlasGenerator.AddInstance(instances[i].first.Get(), instances[i].second, i, 0);
    }

    UINT64 scratchSizeInBytes = 0;
    UINT64 resultSizeInBytes = 0;
    UINT64 instanceDescsSize = 0;
    tlasGenerator.ComputeASBufferSizes(mRtContext->getFallbackDevice(), true, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);

    auto device = m_deviceResources->GetD3DDevice();
    AccelerationStructureBuffers buffers;
    buffers.ResultDataMaxSizeInBytes = resultSizeInBytes;
    
    // Allocate on default heap since the build is done on GPU
    buffers.scratch = nv_helpers_dx12::CreateBuffer(
        device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps);

    D3D12_RESOURCE_STATES initialResourceState = mRtContext->getFallbackDevice()->GetAccelerationStructureResourceState();
    buffers.accelerationStructure = nv_helpers_dx12::CreateBuffer(
        device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        initialResourceState, nv_helpers_dx12::kDefaultHeapProps);
    
    buffers.instanceDesc = nv_helpers_dx12::CreateBuffer(
        device, instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, 
        D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 

    auto commandList = m_deviceResources->GetCommandList();
    tlasGenerator.Generate(commandList, mRtContext->getFallbackCommandList(),
        buffers.scratch.Get(), buffers.accelerationStructure.Get(), buffers.instanceDesc.Get(), 
        [&](ID3D12Resource *resource, UINT bufferNumElements) -> WRAPPED_GPU_POINTER {
            return mRtContext->createFallbackWrappedPointer(resource, bufferNumElements);
    });

    return buffers;
}

void DXRFrameworkApp::CreateAccelerationStructures()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto commandAllocator = m_deviceResources->GetCommandAllocator();

    // Reset the command list for the acceleration structure construction.
    commandList->Reset(commandAllocator, nullptr);

    // Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
    mRtContext->bindDescriptorHeap();

    std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers;
    vertexBuffers.emplace_back(std::make_pair(mVertexBuffer, 3));

    AccelerationStructureBuffers blas = CreateBottomLevelAS(vertexBuffers);

    mInstances.emplace_back(std::make_pair(blas.accelerationStructure, XMMatrixIdentity()));

    AccelerationStructureBuffers tlas = CreateTopLevelAS(mInstances);
    mTlasWrappedPointer = mRtContext->createFallbackWrappedPointer(tlas.accelerationStructure.Get(), tlas.ResultDataMaxSizeInBytes / sizeof(UINT32));

    m_deviceResources->ExecuteCommandList();
    m_deviceResources->WaitForGpu();

    // Retain the bottom level AS result buffer and release the rest of the buffers
    mBlasBuffer = blas.accelerationStructure;
    mTlasBuffer = tlas.accelerationStructure;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();

    mRtBindings->apply(mRtContext, mRtState);

    mRtContext->bindDescriptorHeap();

    commandList->SetComputeRootSignature(mRtProgram->getGlobalRootSignature());
    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputResourceUAVGpuDescriptor);
    mRtContext->getFallbackCommandList()->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, mTlasWrappedPointer);

    mRtContext->raytrace(mRtBindings, mRtState, GetWidth(), GetHeight());
}

void DXRFrameworkApp::CopyRaytracingOutputToBackbuffer()
{
    auto commandList= m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, mOutputResource.Get());

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::OnUpdate()
{
    DXSample::OnUpdate();

    float elapsedTime = static_cast<float>(mTimer.GetElapsedSeconds());
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();
}

void DXRFrameworkApp::OnRender()
{
    if (!m_deviceResources->IsWindowVisible()) {
        return;
    }

    m_deviceResources->Prepare();

    if (mRaytracingEnabled) {
        DoRaytracing();
        CopyRaytracingOutputToBackbuffer();

        m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);
    } else {
        auto commandList= m_deviceResources->GetCommandList();
        auto rtvHandle = m_deviceResources->GetRenderTargetView();

        const float clearColor[] = { 0.3f, 0.2f, 0.1f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        m_deviceResources->Present(D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
}

void DXRFrameworkApp::OnKeyDown(UINT8 key)
{
    switch (key) {
    case VK_SPACE:
        mRaytracingEnabled ^= true;
        break;
    default:
        break;
    }
}

void DXRFrameworkApp::OnDestroy()
{
    m_deviceResources->WaitForGpu();
    OnDeviceLost();
}

void DXRFrameworkApp::OnDeviceLost()
{
    ReleaseWindowSizeDependentResources();
    ReleaseDeviceDependentResources();
}

void DXRFrameworkApp::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

void DXRFrameworkApp::OnSizeChanged(UINT width, UINT height, bool minimized)
{
    if (!m_deviceResources->WindowSizeChanged(width, height, minimized)) {
        return;
    }

    UpdateForSizeChange(width, height);

    ReleaseWindowSizeDependentResources();
    CreateWindowSizeDependentResources();
}
