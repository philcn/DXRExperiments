#include "stdafx.h"
#include "DenoiseCompositor.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "DXSampleHelper.h"
#include <D3Dcompiler.h>

using nv_helpers_dx12::RootSignatureGenerator;

DenoiseCompositor::DenoiseCompositor(DXRFramework::RtContext::SharedPtr context)
    : mRtContext(context)
{
    auto device = context->getDevice();

    ComPtr<ID3DBlob> computeShader;
    ComPtr<ID3DBlob> error;

    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    ThrowIfFailed(D3DCompileFromFile(L"..\\assets\\shaders\\Compute.hlsl", nullptr, nullptr, "main", "cs_5_0", compileFlags, 0, &computeShader, &error));

    RootSignatureGenerator rsConfig;
    rsConfig.AddHeapRangesParameter({ {0 /* u0 */, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0} });
    mComputeRootSignature = rsConfig.Generate(device, false);

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = mComputeRootSignature.Get();
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mComputeState)));
    NAME_D3D12_OBJECT(mComputeState);
}

void DenoiseCompositor::dispatch(ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle, UINT width, UINT height)
{
    mRtContext->bindDescriptorHeap();

    commandList->SetPipelineState(mComputeState.Get());
    commandList->SetComputeRootSignature(mComputeRootSignature.Get());
    commandList->SetComputeRootDescriptorTable(0, outputUavHandle);

    commandList->Dispatch(width / 8, height / 8, 1);
}
