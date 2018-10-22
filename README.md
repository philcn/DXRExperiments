# DXRExperiments

A lightweight prototyping framework for DirectX Raytracing build upon Microsoft DXR Fallback Layer.

![Screenshot](./screenshots/progressive.png?raw=true "Progressive Raytracing")

## Overview

The main purpose of this project is to setup a prototyping environment for learning DirectX Raytracing, and hybrid raytracing rendering in general. The project absorbed a lot of code from Microsoft's early DXR samples and Nvidia's DXR tutorials, and includes a DXR API wrapper designed by borrowing many concepts from the Nvidia Falcor framework. 

## Features

* Windows 10 RS5 DXR API compliance
* Accumulation based progressive raytracing pipeline
* Supports multiple mesh file formats including OBJ, FBX and Collada
* Currently uses a basic phong BRDF
* WIP Realtime raytracing pipeline with denoise filter

## Dependencies

I try to minimize dependencies by not introducing a third-party D3D12 renderer / abstraction layer, thus the DXR API wrapper works directly with D3D12 and is rather inefficient in doing anything other than raytracing. I use DirectXTK for texture loading, Assimp for mesh loading, imgui for UI, and a subset of Microsoft MiniEngine for math and user input.

The DXR Fallback Layer submodule points to my fork which only differs from the Microsoft version in configuration.

## Compatibility

This project uses Microsoft DXR Fallback Layer for DXR API calls, thus works for non-RTX GPUs via compute emulation path. On RTX GPUs, the fallback layer simply forwards calls to native DXR API, and can achieve the same performance as directly using native API.

The support for non-Nvidia GPUs are limited as per DXR Fallback Layer. 

## Setup Guide

Clone the Github repo, then initialize the submodules with

```
$ git submodule update --init
```

Download Microsoft DXR Fallback Compiler binaries with the link in the following section. Copy all the contents to `externals/D3D12RaytracingFallback/Bin/x64`.

Open `DXRExperiments.sln` and build `DXRExperiments` project.

## Requirements

DXRExperiments is maintained to run on the following environment:

* Windows 10 1809 (18252.1000) or higher.
* Visual Studio 2017 version 15.8.6 or higher.
* Windows 10 10.0.17763.0 SDK or higher.
* DXR Fallback Compiler - [DirectXRaytracingBinariesV1.5.zip](https://github.com/Microsoft/DirectX-Graphics-Samples/releases/tag/v1.5-dxr).
* Nvidia Maxwell or Turing GPU.

## Reference

https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3D12RaytracingFallback
https://github.com/Microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing
https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-1
https://github.com/NVIDIAGameWorks/Falcor
http://intro-to-dxr.cwyman.org/
