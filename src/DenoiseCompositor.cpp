#include "stdafx.h"
#include "DenoiseCompositor.h"
#include "CompiledShaders/DenoiseCompositorH.hlsl.h"
#include "CompiledShaders/DenoiseCompositorV.hlsl.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "DXSampleHelper.h"
#include "DirectXRaytracingHelper.h"
#include "WICTextureLoader.h"
#include "ResourceUploadBatch.h"
#include "Math/Common.h"
#include "ImGuiRendererDX.h"
#include <D3Dcompiler.h>

using nv_helpers_dx12::RootSignatureGenerator;

DenoiseCompositor::DenoiseCompositor(DXRFramework::RtContext::SharedPtr context)
    : mRtContext(context), mActive(true)
{
    auto device = context->getDevice();

    RootSignatureGenerator rsConfig;
    rsConfig.AddHeapRangesParameter({ {0 /* t0 */, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0} });
    rsConfig.AddHeapRangesParameter({ {1 /* t1 */, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0} });
    rsConfig.AddHeapRangesParameter({ {0 /* u0 */, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0} });
    rsConfig.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0 /* b0 */, 0, 1);

    mComputeRootSignature = rsConfig.Generate(device, false);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = mComputeRootSignature.Get();
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pDenoiseCompositorH, ARRAYSIZE(g_pDenoiseCompositorH));

    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mComputeState[0])));
    NAME_D3D12_OBJECT(mComputeState[0]);

    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pDenoiseCompositorV, ARRAYSIZE(g_pDenoiseCompositorV));
    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mComputeState[1])));
    NAME_D3D12_OBJECT(mComputeState[1]);
}

void DenoiseCompositor::loadResources(ID3D12CommandQueue *uploadCommandQueue, UINT frameCount, bool loadMockResources)
{
    auto device = mRtContext->getDevice();

    mConstantBuffer.Create(device, frameCount, L"DenoiseCompositorCB");
    mConstantBuffer->exposure = 1.0f;
    mConstantBuffer->gamma = 2.2f;
    mConstantBuffer->tonemap = true;
    mConstantBuffer->gammaCorrect = false;
    mConstantBuffer->maxKernelSize = 12;
    mConstantBuffer->debugVisualize = 0;

    if (loadMockResources) {
        mTextureResources.resize(2);
        mTextureSrvGpuHandles.resize(2);

        using namespace DirectX;
        ResourceUploadBatch resourceUpload(device);
        resourceUpload.Begin();
        {
            ThrowIfFailed(CreateWICTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\DirectLighting.png", &mTextureResources[0]));
            ThrowIfFailed(CreateWICTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\IndirectSpecular.png", &mTextureResources[1]));
        }
        auto uploadResourcesFinished = resourceUpload.End(uploadCommandQueue);
        uploadResourcesFinished.wait();

        mTextureSrvGpuHandles[0] = mRtContext->createTextureSRVHandle(mTextureResources[0].Get()); 
        mTextureSrvGpuHandles[1] = mRtContext->createTextureSRVHandle(mTextureResources[1].Get()); 
    }
}

void DenoiseCompositor::createOutputResource(DXGI_FORMAT format, UINT width, UINT height)
{
    auto device = mRtContext->getDevice();

    for (int i = 0; i < 2; ++i) {
        AllocateUAVTexture(device, format, width, height, mOutputResource[i].ReleaseAndGetAddressOf(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle;
            mOutputUavHeapIndex[i] = mRtContext->allocateDescriptor(&uavCpuHandle, mOutputUavHeapIndex[i]);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(mOutputResource[i].Get(), nullptr, &uavDesc, uavCpuHandle);

            mOutputUavGpuHandle[i] = mRtContext->getDescriptorGPUHandle(mOutputUavHeapIndex[i]);
        }

        {
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle;
            mOutputSrvHeapIndex[i] = mRtContext->allocateDescriptor(&srvCpuHandle, mOutputSrvHeapIndex[i]);
            mOutputSrvGpuHandle[i] = mRtContext->createTextureSRVHandle(mOutputResource[i].Get(), false, mOutputSrvHeapIndex[i]);
        }
    }
}

void DenoiseCompositor::userInterface()
{
    ui::Begin("Denoiser");
    ui::SliderFloat("Exposure", &mConstantBuffer->exposure, 0.0f, 10.0f);
    ui::SliderFloat("Gamma", &mConstantBuffer->gamma, 1.0f, 3.0f);
    ui::Checkbox("Tonemap", (bool*)&mConstantBuffer->tonemap);
    ui::Checkbox("Gamma correct", (bool*)&mConstantBuffer->gammaCorrect);
    ui::SliderInt("Max Kernel Size", &mConstantBuffer->maxKernelSize, 1, 25);
    ui::SliderInt("Debug Visualize", (int*)&mConstantBuffer->debugVisualize, 0, 3);
    ui::End();
}

void DenoiseCompositor::dispatch(ID3D12GraphicsCommandList *commandList, DenoiseCompositor::InputComponents inputs, UINT frameIndex, UINT width, UINT height)
{
    mConstantBuffer.CopyStagingToGpu(frameIndex);

    if (inputs.directLightingSrv.ptr == 0) {
        inputs.directLightingSrv = mTextureSrvGpuHandles[0];
        inputs.indirectSpecularSrv = mTextureSrvGpuHandles[1];
    }

    mRtContext->bindDescriptorHeap();

    commandList->SetComputeRootSignature(mComputeRootSignature.Get());
    commandList->SetComputeRootConstantBufferView(3, mConstantBuffer.GpuVirtualAddress(frameIndex));

    const UINT DispatchGroupWidth = 64;

    // pass 0
    {
        commandList->SetPipelineState(mComputeState[0].Get());
        commandList->SetComputeRootDescriptorTable(0, inputs.directLightingSrv);
        commandList->SetComputeRootDescriptorTable(1, inputs.indirectSpecularSrv);
        commandList->SetComputeRootDescriptorTable(2, mOutputUavGpuHandle[0]);
        commandList->Dispatch(Math::DivideByMultiple(width, DispatchGroupWidth), height, 1);

        mRtContext->transitionResource(mOutputResource[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mRtContext->insertUAVBarrier(mOutputResource[0].Get());
    }

    // pass 1
    {
        commandList->SetPipelineState(mComputeState[1].Get());
        commandList->SetComputeRootDescriptorTable(0, inputs.directLightingSrv);
        commandList->SetComputeRootDescriptorTable(1, mOutputSrvGpuHandle[0]);
        commandList->SetComputeRootDescriptorTable(2, mOutputUavGpuHandle[1]);
        commandList->Dispatch(width, Math::DivideByMultiple(height, DispatchGroupWidth), 1);

        mRtContext->transitionResource(mOutputResource[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        mRtContext->insertUAVBarrier(mOutputResource[1].Get());
    }
}
