# DXRFramework

Implemented the Nvidia DXR tutorial with DXR Fallback Layer, tested on Nvidia GTX 980Ti (AMD won't work). Extended helpers in nv_helpers_dx12 to work with both D3D12 prototype device and fallback device.

## Fallback Layer workaround

Due to the limitations of Fallback Layer, we have to use a slightly different raytracing pipeline and resource binding layout than the tutorial. 

* Put top level acceleration structure and output UAV in global root signature rather than the local root signature of RayGen shader
    * That's because fallback layer requires to use a special `SetTopLevelAccelerationStructure()` routine to bind the acceleration structure, rather than using `SetComputeRootShaderResourceView()` or local root signature
* Use empty local root signatures, or only use root constants in local root signatures
    * Root descriptor support in local root signature is not enabled by default in fallback layer; the Microsoft DXR samples only uses root constants. Descriptor table support hasn't been tested, so keep the local root signatures simple for now
* Need to bind the descriptor heap when 1) building acceleration structures and 2) calling DispatchRays(). That's for the wrapped pointer to work (see Microsoft Fallback Layer documentation)

## Reference

https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3D12RaytracingFallback
https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing

https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-1
https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-2

https://github.com/NVIDIAGameWorks/DxrTutorials

http://intro-to-dxr.cwyman.org/
