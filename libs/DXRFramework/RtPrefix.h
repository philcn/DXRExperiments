#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <sstream>
#include <iomanip>

#include <list>
#include <string>
#include <wrl.h>
#include <shellapi.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <atlbase.h>
#include <assert.h>

#include <dxgi1_6.h>
#include "d3d12.h"
#include <atlbase.h>
#include "D3D12RaytracingFallback.h"
#include "D3D12RaytracingHelpers.hpp"
#include "d3dx12.h"

#include <DirectXMath.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include "DXSampleHelper.h"
#include "DeviceResources.h"

