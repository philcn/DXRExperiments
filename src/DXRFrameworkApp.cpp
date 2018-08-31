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
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"   
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

#include "CompiledShaders/Raytracing.hlsl.h"

using namespace std;
using namespace DX;

namespace GlobalRootSignatureParams {
    enum Value {
        AccelerationStructureSlot = 0,
        OutputViewSlot,
        Count 
    };
}

DXRFrameworkApp::DXRFrameworkApp(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    mUseDXRDriver(false),
    mRaytracingEnabled(true),
    mDescriptorsAllocated(0)
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
    EnableDXRExperimentalFeatures(m_deviceResources->GetAdapter());

    m_deviceResources->CreateDeviceResources();
    m_deviceResources->CreateWindowSizeDependentResources();

    InitializeScene();

    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

void DXRFrameworkApp::InitializeScene()
{
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
}

void DXRFrameworkApp::CreateDeviceDependentResources()
{
    CreateRaytracingInterfaces();

    CreateGlobalRootSignature();

    CreateRaytracingPipeline();

    CreateDescriptorHeap();

    CreateGeometries();

    CreateAccelerationStructures();

    // CreateConstantBuffers();

    CreateShaderBindingTable(); 
}

void DXRFrameworkApp::CreateWindowSizeDependentResources()
{
    CreateRaytracingOutputBuffer();

    // UpdateCameraMatrices();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::CreateRaytracingInterfaces()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();

    // Only support fallback layer now
    if (!mUseDXRDriver) {
        CreateRaytracingFallbackDeviceFlags createDeviceFlags = CreateRaytracingFallbackDeviceFlags::None;
        ThrowIfFailed(D3D12CreateRaytracingFallbackDevice(device, createDeviceFlags, 0, IID_PPV_ARGS(&mFallbackDevice)));
        mFallbackDevice->QueryRaytracingCommandList(commandList, IID_PPV_ARGS(&mFallbackCommandList));
    } else { 
        assert(0); // DirectX Raytracing
    }
}

void DXRFrameworkApp::CreateDescriptorHeap()
{
    auto device = m_deviceResources->GetD3DDevice();

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    // Allocate a heap for 3 descriptors:
    // 1 - raytracing output texture SRV
    // 2 - bottom and top level acceleration structure fallback wrapped pointer UAVs
    descriptorHeapDesc.NumDescriptors = 3; 
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptorHeapDesc.NodeMask = 0;
    device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&mDescriptorHeap));
    NAME_D3D12_OBJECT(mDescriptorHeap);

    mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
    blasGenerator.ComputeASBufferSizes(mFallbackDevice.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);

    auto device = m_deviceResources->GetD3DDevice();
    AccelerationStructureBuffers buffers;

    buffers.scratch = nv_helpers_dx12::CreateBuffer(
        device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps);

    D3D12_RESOURCE_STATES initialResourceState = mFallbackDevice->GetAccelerationStructureResourceState();
    buffers.accelerationStructure = nv_helpers_dx12::CreateBuffer(
        device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        initialResourceState, nv_helpers_dx12::kDefaultHeapProps);

    auto commandList = m_deviceResources->GetCommandList();
    blasGenerator.Generate(commandList, mFallbackCommandList.Get(), buffers.scratch.Get(), buffers.accelerationStructure.Get());

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
    tlasGenerator.ComputeASBufferSizes(mFallbackDevice.Get(), true, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);

    auto device = m_deviceResources->GetD3DDevice();
    AccelerationStructureBuffers buffers;
    buffers.ResultDataMaxSizeInBytes = resultSizeInBytes;
    
    // Allocate on default heap since the build is done on GPU
    buffers.scratch = nv_helpers_dx12::CreateBuffer(
        device, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps);

    D3D12_RESOURCE_STATES initialResourceState = mFallbackDevice->GetAccelerationStructureResourceState();
    buffers.accelerationStructure = nv_helpers_dx12::CreateBuffer(
        device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        initialResourceState, nv_helpers_dx12::kDefaultHeapProps);
    
    buffers.instanceDesc = nv_helpers_dx12::CreateBuffer(
        device, instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, 
        D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 

    auto commandList = m_deviceResources->GetCommandList();
    tlasGenerator.Generate(commandList, mFallbackCommandList.Get(), 
        buffers.scratch.Get(), buffers.accelerationStructure.Get(), buffers.instanceDesc.Get(), 
        [&](ID3D12Resource *resource, UINT bufferNumElements) -> WRAPPED_GPU_POINTER {
            return CreateFallbackWrappedPointer(resource, bufferNumElements);
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
    ID3D12DescriptorHeap *pDescriptorHeaps[] = { mDescriptorHeap.Get() };
    mFallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

    std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers;
    vertexBuffers.emplace_back(std::make_pair(mVertexBuffer, 3));

    AccelerationStructureBuffers blas = CreateBottomLevelAS(vertexBuffers);

    mInstances.emplace_back(std::make_pair(blas.accelerationStructure, XMMatrixIdentity()));

    AccelerationStructureBuffers tlas = CreateTopLevelAS(mInstances);
    mTlasWrappedPointer = CreateFallbackWrappedPointer(tlas.accelerationStructure.Get(), tlas.ResultDataMaxSizeInBytes / sizeof(UINT32));

    m_deviceResources->ExecuteCommandList();
    m_deviceResources->WaitForGpu();

    // Retain the bottom level AS result buffer and release the rest of the buffers
    mBlasBuffer = blas.accelerationStructure;
    mTlasBuffer = tlas.accelerationStructure;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::CreateGlobalRootSignature()
{
#if 1
    nv_helpers_dx12::RootSignatureGenerator rootSigGenerator;
    // slot 0, GlobalRootSignatureParams::AccelerationStructureSlot
    rootSigGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0);
    // slot 1, GlobalRootSignatureParams::OutputViewSlot
    rootSigGenerator.AddHeapRangesParameter({
        {0 /* u0 */, 1 /* 1 descriptor */, 0 /* space 0 */, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0 /* offset to heap root argument */}
    });
    mGlobalRootSignature = rootSigGenerator.Generate(mFallbackDevice.Get(), false /* local root signature */);
#else
    CD3DX12_DESCRIPTOR_RANGE ranges[1]; // Perfomance TIP: Order from most frequent to least frequent.
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 output texture

    CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
    rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
    rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
    CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
    SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &mGlobalRootSignature);
#endif
}

ComPtr<ID3D12RootSignature> DXRFrameworkApp::CreateRayGenSignature()
{
    nv_helpers_dx12::RootSignatureGenerator rootSigGenerator;
    return rootSigGenerator.Generate(mFallbackDevice.Get(), true);
}

ComPtr<ID3D12RootSignature> DXRFrameworkApp::CreateMissSignature()
{
    nv_helpers_dx12::RootSignatureGenerator rootSigGenerator;
    return rootSigGenerator.Generate(mFallbackDevice.Get(), true);
}

ComPtr<ID3D12RootSignature> DXRFrameworkApp::CreateHitSignature()
{
    nv_helpers_dx12::RootSignatureGenerator rootSigGenerator;
    return rootSigGenerator.Generate(mFallbackDevice.Get(), true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::CreateRaytracingPipeline()
{
    auto device = m_deviceResources->GetD3DDevice();
    nv_helpers_dx12::RayTracingPipelineGenerator pipeline(device, mFallbackDevice.Get());

    // Load DXIL libraries
    pipeline.AddLibrary(g_pRaytracing, ARRAYSIZE(g_pRaytracing), {L"RayGen", L"Miss", L"ClosestHit"});

    // Create local root signatures
    mRayGenSignature = CreateRayGenSignature();
    mMissSignature = CreateMissSignature();
    mHitSignature = CreateHitSignature();

    pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");

    // The following section associates the root signature to each shader. Note that we can explicitly
    // show that some shaders share the same root signature (eg. Miss and ShadowMiss). Note that the
    // hit shaders are now only referred to as hit groups, meaning that the underlying intersection,
    // any-hit and closest-hit shaders share the same root signature.
    pipeline.AddRootSignatureAssociation(mRayGenSignature.Get(), {L"RayGen"});
    pipeline.AddRootSignatureAssociation(mMissSignature.Get(), {L"Miss"});
    pipeline.AddRootSignatureAssociation(mHitSignature.Get(), {L"HitGroup"});

    // The payload size defines the maximum size of the data carried by the rays, ie. the the data
    // exchanged between shaders, such as the HitInfo structure in the HLSL code. It is important to
    // keep this value as low as possible as a too high value would result in unnecessary memory
    // consumption and cache trashing.
    pipeline.SetMaxPayloadSize(4 * sizeof(float)); // RGB + distance

    // Upon hitting a surface, DXR can provide several attributes to the hit. In our sample we just
    // use the barycentric coordinates defined by the weights u,v of the last two vertices of the
    // triangle. The actual barycentrics can be obtained using float3 barycentrics = float3(1.f-u-v,
    // u, v);
    pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

    // The raytracing process can shoot rays from existing hit points, resulting in nested TraceRay
    // calls. Our sample code traces only primary rays, which then requires a trace depth of 1. Note
    // that this recursion depth should be kept to a minimum for best performance. Path tracing
    // algorithms can be easily flattened into a simple loop in the ray generation.
    pipeline.SetMaxRecursionDepth(1);

    mFallbackStateObject = pipeline.FallbackGenerate(mGlobalRootSignature.Get());
}

void DXRFrameworkApp::CreateShaderBindingTable()
{
    auto device = m_deviceResources->GetD3DDevice();

    mShaderTableGenerator.Reset();

    // Example of specifying root arguments for local root signature. Note that in fallback layer  
    // root descriptors must be WRAPPED_GPU_POINTER's rather than D3D12_GPU_VIRTUAL_ADDRESS.
    // D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    // auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);
    // mShaderTableGenerator.AddHitGroup(L"HitGroup", {heapPointer});

    mShaderTableGenerator.AddRayGenerationProgram(L"RayGen", {});
    mShaderTableGenerator.AddMissProgram(L"Miss", {});
    mShaderTableGenerator.AddHitGroup(L"HitGroup", {});

    UINT32 shaderTableSize = mShaderTableGenerator.ComputeSBTSize(mFallbackDevice.Get());

    // Create the SBT on the upload heap. This is required as the helper will use mapping to write the
    // SBT contents. After the SBT compilation it could be copied to the default heap for performance.
    mShaderTable = nv_helpers_dx12::CreateBuffer(device, shaderTableSize, D3D12_RESOURCE_FLAG_NONE,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, 
                                                 nv_helpers_dx12::kUploadHeapProps);

    mShaderTableGenerator.Generate(mShaderTable.Get(), mFallbackStateObject.Get());
}

void DXRFrameworkApp::CreateRaytracingOutputBuffer()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    AllocateUAVTexture(device, backbufferFormat, m_width, m_height, &mOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    UINT outputResourceUAVDescriptorHeapIndex = AllocateDescriptor(&uavDescriptorHandle, UINT_MAX);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    mOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), outputResourceUAVDescriptorHeapIndex, mDescriptorSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::ReleaseWindowSizeDependentResources()
{
    mOutputResource.Reset();
}

void DXRFrameworkApp::ReleaseDeviceDependentResources()
{
    mFallbackDevice.Reset();
    mFallbackCommandList.Reset();
    mFallbackStateObject.Reset();

    mDescriptorHeap.Reset();
    mVertexBuffer.Reset();
    mBlasBuffer.Reset();
    mTlasBuffer.Reset();
    
    mGlobalRootSignature.Reset();
    mRayGenSignature.Reset();
    mHitSignature.Reset();
    mMissSignature.Reset();

    mShaderTable.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::DoRaytracing()
{
    D3D12_FALLBACK_DISPATCH_RAYS_DESC desc = {};

    // The ray generation shaders are always at the beginning of the SBT. 
    uint32_t rayGenerationSectionSizeInBytes = mShaderTableGenerator.GetRayGenSectionSize();
    desc.RayGenerationShaderRecord.StartAddress = mShaderTable->GetGPUVirtualAddress();
    desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

    // The miss shaders are in the second SBT section, right after the ray
    // generation shader. We have one miss shader for the camera rays and one
    // for the shadow rays, so this section has a size of 2*m_sbtEntrySize. We
    // also indicate the stride between the two miss shaders, which is the size
    // of a SBT entry
    uint32_t missSectionSizeInBytes = mShaderTableGenerator.GetMissSectionSize();
    desc.MissShaderTable.StartAddress = mShaderTable->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
    desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
    desc.MissShaderTable.StrideInBytes = mShaderTableGenerator.GetMissEntrySize();

    // The hit groups section start after the miss shaders. In this sample we
    // have one 1 hit group for the triangle
    uint32_t hitGroupsSectionSize = mShaderTableGenerator.GetHitGroupSectionSize();
    desc.HitGroupTable.StartAddress = mShaderTable->GetGPUVirtualAddress() +
        rayGenerationSectionSizeInBytes +
        missSectionSizeInBytes;
    desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
    desc.HitGroupTable.StrideInBytes = mShaderTableGenerator.GetHitGroupEntrySize();

    // Dimensions of the image to render, identical to a kernel launch dimension
    desc.Width = GetWidth();
    desc.Height = GetHeight();

    ID3D12DescriptorHeap *pDescriptorHeaps[] = { mDescriptorHeap.Get() };
    mFallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

    auto commandList = m_deviceResources->GetCommandList();
    commandList->SetComputeRootSignature(mGlobalRootSignature.Get());
    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mOutputResourceUAVGpuDescriptor);
    mFallbackCommandList->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, mTlasWrappedPointer);

    mFallbackCommandList->DispatchRays(mFallbackStateObject.Get(), &desc);
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
    mTimer.Tick();
    CalculateFrameStats();
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DXRFrameworkApp::CalculateFrameStats()
{
    static int frameCnt = 0;
    static double elapsedTime = 0.0f;
    double totalTime = mTimer.GetTotalSeconds();
    frameCnt++;

    if ((totalTime - elapsedTime) >= 1.0f) {
        float diff = static_cast<float>(totalTime - elapsedTime);
        float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

        frameCnt = 0;
        elapsedTime = totalTime;

        float MRaysPerSecond = (m_width * m_height * fps) / static_cast<float>(1e6);

        wstringstream windowText;

        if (mFallbackDevice->UsingRaytracingDriver()) {
            windowText << L"(FL-DXR)";
        } else {
            windowText << L"(FL)";
        }
        windowText << setprecision(2) << fixed
            << L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
            << L"    GPU[" << m_deviceResources->GetAdapterID() << L"]: " << m_deviceResources->GetAdapterDescription();
        SetCustomWindowText(windowText.str().c_str());
    }
}

void DXRFrameworkApp::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
    auto device = m_deviceResources->GetD3DDevice();
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    ThrowIfFailed(mFallbackDevice->D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
    ThrowIfFailed(mFallbackDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
}

// Create a wrapped pointer for the Fallback Layer path.
WRAPPED_GPU_POINTER DXRFrameworkApp::CreateFallbackWrappedPointer(ID3D12Resource* resource, UINT bufferNumElements)
{
    auto device = m_deviceResources->GetD3DDevice();

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawBufferUavDesc = {};
    rawBufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawBufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    rawBufferUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawBufferUavDesc.Buffer.NumElements = bufferNumElements;

    D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;
   
    // Only compute fallback requires a valid descriptor index when creating a wrapped pointer.
    UINT descriptorHeapIndex = 0;
    if (!mFallbackDevice->UsingRaytracingDriver()) {
        descriptorHeapIndex = AllocateDescriptor(&bottomLevelDescriptor);
        device->CreateUnorderedAccessView(resource, nullptr, &rawBufferUavDesc, bottomLevelDescriptor);
    }
    return mFallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
}

// Allocate a descriptor and return its index. 
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT DXRFrameworkApp::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
    auto descriptorHeapCpuBase = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (descriptorIndexToUse >= mDescriptorHeap->GetDesc().NumDescriptors) {
        descriptorIndexToUse = mDescriptorsAllocated++;
    }
    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, mDescriptorSize);
    return descriptorIndexToUse;
}

void DXRFrameworkApp::EnableDXRExperimentalFeatures(IDXGIAdapter1* adapter)
{
    // DXR is an experimental feature and needs to be enabled before creating a D3D12 device.
    mUseDXRDriver = EnableRaytracing(adapter);

    if (!mUseDXRDriver) {
        OutputDebugString(
            L"Could not enable raytracing driver (D3D12EnableExperimentalFeatures() failed).\n" \
            L"Possible reasons:\n" \
            L"  1) your OS is not in developer mode.\n" \
            L"  2) your GPU driver doesn't match the D3D12 runtime loaded by the app (d3d12.dll and friends).\n" \
            L"  3) your D3D12 runtime doesn't match the D3D12 headers used by your app (in particular, the GUID passed to D3D12EnableExperimentalFeatures).\n\n");

        OutputDebugString(L"Enabling compute based fallback raytracing support.\n");
        ThrowIfFalse(EnableComputeRaytracingFallback(adapter), L"Could not enable compute based fallback raytracing support (D3D12EnableExperimentalFeatures() failed).\n");
    }
}
