#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>
#include <atlbase.h>
#include <wrl.h>
#include <shellapi.h>

#include <sstream>
#include <iomanip>

#include "d3d12.h"
#include "d3dx12.h"
#include <dxgi1_6.h>
#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include "Helpers/DirectXHelper.h"
#include "DeviceResources.h"
