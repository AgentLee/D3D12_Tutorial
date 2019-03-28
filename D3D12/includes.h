#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

#if defined(CreateWindow)
#undef CreateWindow
#endif

#include <wrl.h>
using namespace Microsoft::WRL;

#include <d3d12.h>
#include <dxgi1_6.h>			// low level task management
#include <d3dcompiler.h>		// all the header files to compile shaders at runtime
#include <DirectXMath.h>
#include <d3dx12.h>				// useful classes that can simplify function calling

#include <algorithm>
#include <cassert>
#include <chrono>

#include "Helper.h"
