#include "stdafx.h"
#include "DenoiseCompositor.h"
#include "CompiledShaders/Compute.hlsl.h"
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
    rsConfig.AddHeapRangesParameter({ {0 /* u0 */, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0} });
    rsConfig.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0 /* b0 */, 0, 1);
    rsConfig.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 1 /* b1 */, 0, 1);

    mComputeRootSignature = rsConfig.Generate(device, false);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = mComputeRootSignature.Get();
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pCompute, ARRAYSIZE(g_pCompute));

    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mComputeState)));
    NAME_D3D12_OBJECT(mComputeState);
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

    if (loadMockResources) {
        mTextureResources.resize(1);
        mTextureSrvGpuHandles.resize(1);

        using namespace DirectX;
        ResourceUploadBatch resourceUpload(device);
        resourceUpload.Begin();
        {
            ThrowIfFailed(CreateWICTextureFromFile(device, resourceUpload, L"..\\assets\\textures\\linearImage.png", &mTextureResources[0], true));
        }
        auto uploadResourcesFinished = resourceUpload.End(uploadCommandQueue);
        uploadResourcesFinished.wait();

        mTextureSrvGpuHandles[0] = mRtContext->createTextureSRVHandle(mTextureResources[0].Get()); 
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
    ui::End();
}

void DenoiseCompositor::dispatch(ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle, UINT frameIndex, UINT width, UINT height)
{
    mConstantBuffer.CopyStagingToGpu(frameIndex);

    if (inputSrvHandle.ptr == 0) {
        inputSrvHandle = mTextureSrvGpuHandles[0];
    }

    mRtContext->bindDescriptorHeap();

    commandList->SetPipelineState(mComputeState.Get());
    commandList->SetComputeRootSignature(mComputeRootSignature.Get());
    commandList->SetComputeRootConstantBufferView(2, mConstantBuffer.GpuVirtualAddress(frameIndex));

    const UINT TileSize = 16;

    // pass 0
    {
        commandList->SetComputeRootDescriptorTable(0, inputSrvHandle);
        commandList->SetComputeRootDescriptorTable(1, mOutputUavGpuHandle[0]);
        commandList->SetComputeRoot32BitConstant(3, 0, 0);
        commandList->Dispatch(Math::DivideByMultiple(width, TileSize), Math::DivideByMultiple(height, TileSize), 1);

        mRtContext->transitionResource(mOutputResource[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mRtContext->insertUAVBarrier(mOutputResource[0].Get());
    }

    // pass 1
    {
        commandList->SetComputeRootDescriptorTable(0, mOutputSrvGpuHandle[0]);
        commandList->SetComputeRootDescriptorTable(1, mOutputUavGpuHandle[1]);
        commandList->SetComputeRoot32BitConstant(3, 1, 0);
        commandList->Dispatch(Math::DivideByMultiple(width, TileSize), Math::DivideByMultiple(height, TileSize), 1);

        mRtContext->transitionResource(mOutputResource[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        mRtContext->insertUAVBarrier(mOutputResource[1].Get());
    }
}
