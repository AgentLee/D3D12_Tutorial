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

const uint8_t gNumFrames = 3;	// number of swap chain back buffers - triple buffering
bool gUseWarp = false;			// use WARP adapter (software rasterizer)

uint32_t gClientWidth = 1280;
uint32_t gClientHeight = 1080;

bool gIsInitialized = false;

HWND gHWnd;			// handle to window that displays the rendered image
RECT gWindowRect;	// stores previous window state to handle full screen mode switching

// D3D12 objects
ComPtr<ID3D12Device2> gDevice;						// directx device object
ComPtr<ID3D12CommandQueue> gCommandQueue;
ComPtr<IDXGISwapChain4> gSwapChain;
ComPtr<ID3D12Resource> gBackBuffers[gNumFrames];	// used to keep track of which buffer to switch to
													// back buffers are actually textures

ComPtr<ID3D12GraphicsCommandList> gCommandList;		
ComPtr<ID3D12CommandAllocator> gCommandAllocators[gNumFrames];	// backing memory for recording the commands into the command list
																// command allocators cannot be reused until
																// the commands that are already being executed
																// are finished

ComPtr<ID3D12DescriptorHeap> gRTVDescriptorHeap;	// each of these is a back buffer texture in the swap chain
													// RTVs describe the properties of the texture
													// GPU location, size, format, etc.
													// used to clear back buffer 
													// contains RTVs for the swap chain back buffers

UINT gRTVDescriptorSize;
UINT gCurrBackBufferIdx;	// current back buffer index in the swap chain

// Sync objects
ComPtr<ID3D12Fence> gFence;
uint64_t gFenceValue = 0;				// next fence value to signal the command queue
// need to track the fence values that were used
// to signal the command queue.
// this makes sure that the resources being used by the command queue
// don't get overwritten. gFrameFenceValues tracks the fence values 
// that were used during a frame.
uint64_t gFrameFenceValues[gNumFrames];
// receives a notification that the fence value has reached
// the target value. CPU stalls until this target value is reached
HANDLE gFenceEvent;

// Swap chain control
bool gVsync = true;
bool gTearingSupport = false;
bool gFullScreenMode = false;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void ParseCommandLineArguments()
{
	int argc;
	wchar_t **argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

	for (size_t i = 0; i < argc; ++i)
	{
		wchar_t *flag = argv[i];
		wchar_t *value = argv[++i];
		if (::wcscmp(flag, L"-w") == 0 || ::wcscmp(flag, L"-width") == 0)
		{
			gClientWidth = ::wcstol(value, nullptr, 10);
		}
		if (::wcscmp(flag, L"-h") == 0 || ::wcscmp(flag, L"-height") == 0)
		{
			gClientHeight = ::wcstol(value, nullptr, 10);
		}
		if (::wcscmp(flag, L"-warp") == 0 || ::wcscmp(flag, L"--width") == 0)
		{
			gUseWarp = true;
		}

		::LocalFree(argv);
	}
}

// Want to make sure that the device is created 
// AFTER enabling the debug layer - it will be deleted.
// Also catches all the errors in creating D12 objects 
void EnabelDebugLayer()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugInterface;
	// IID_PPV_ARGS gets an interface pointer
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
	debugInterface->EnableDebugLayer();
#endif
}

// Window class must be registered before creating the actual window
// cbSize --> size of the structure
// style --> how the window should be drawn
// lpfnWndProc --> pointer to windows procedure that handles the messages in this class
// cbClsExtra --> extra bytes to allocate class
// cpWndExtra --> extra bytes to allocate instance
// hInstance --> handle to window instance
// hIcon --> 
void RegisterWindowClass(HINSTANCE hInst, const wchar_t *windowClassName)
{
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &WndProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInst;
	windowClass.hIcon = ::LoadIcon(hInst, NULL);
	windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = windowClassName;
	windowClass.hIconSm = ::LoadIcon(hInst, NULL);

	static ATOM atom = ::RegisterClassExW(&windowClass);
	assert(atom > 0);
}

// Creates the window but doesn't show it.
// Will need to create and init the D12 device and command queue.
HWND CreateWindow(const wchar_t *windowClassName, HINSTANCE hInst,
	const wchar_t *windowTitle, uint32_t width, uint32_t height)
{
	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

	// Style window
	RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Set dimensions
	int windowWidth = windowRect.right - windowRect.left;
	int windowHeight = windowRect.bottom - windowRect.top;

	// Window position
	int posX = std::max<int>(0, (screenWidth - windowWidth) / 2);
	int posY = std::max<int>(0, (screenHeight - windowHeight) / 2);

	HWND hWnd = ::CreateWindowExW(
		NULL,
		windowClassName,
		windowTitle,
		WS_OVERLAPPEDWINDOW,
		posX,
		posY,
		windowWidth,
		windowHeight,
		NULL,
		NULL,
		hInst,
		nullptr
	);

	assert(hWnd && "Failed to create window");

	return hWnd;
}

// Determine if the adapter is compatible with D3D12
ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
	ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;

#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> dxgiAdapter1;
	ComPtr<IDXGIAdapter4> dxgiAdapter4;

	if (useWarp)
	{
		ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
		ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
	}
	else
	{
		SIZE_T maxDedicatedVideoMemory = 0;
		for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
				SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
					D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
				dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
			{
				maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
			}
		}
	}

	return dxgiAdapter4;
}

// D12 device is used to create resources like textures, queues, fences, etc.
// It's not for doing draw or dispatch calls.
// Tracks allocations in GPU memory --> destroying it will destroy everything.
// Will need to go over the debug portion
ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
	ComPtr<ID3D12Device2> d3d12Device2;
	ThrowIfFailed(D3D12CreateDevice(
		adapter.Get(),				// pointer to card
		D3D_FEATURE_LEVEL_11_0,		// minimum feature level
		IID_PPV_ARGS(&d3d12Device2) // globally unique identifier for device interface
	));

#if defined(_DEBUG)
	ComPtr<ID3D12InfoQueue> pInfoQueue;		// used to enable break points based on severity level
	if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
	}

	// Filter messages based on severity level
	D3D12_MESSAGE_SEVERITY Severities[] =
	{
		D3D12_MESSAGE_SEVERITY_INFO
	};

	D3D12_MESSAGE_ID DenyIds[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
	}; 
	
	D3D12_INFO_QUEUE_FILTER NewFilter = {};
	NewFilter.DenyList.NumSeverities = _countof(Severities);
	NewFilter.DenyList.pSeverityList = Severities;
	NewFilter.DenyList.NumIDs = _countof(DenyIds);
	NewFilter.DenyList.pIDList = DenyIds;

	ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
#endif

	return d3d12Device2;
}

// Need to go over params D3D12_COMMAND_QUEUE_DESC
ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

	return d3d12CommandQueue;
}

// Tearing occurs when the image is out of sync with the vertical refresh rate.
bool CheckTearingSupport()
{
	BOOL allowTearing = FALSE;

	ComPtr<IDXGIFactory4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
	{
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory4.As(&factory5)))
		{
			if (FAILED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing,
				sizeof(allowTearing)
			)))
			{
				allowTearing = false;
			}
		}
	}

	return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd,
	ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height,
	uint32_t bufferCount)
{
	ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;

#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc = { 1, 0 };
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = bufferCount;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		commandQueue.Get(),
		hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	));

	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

	return dxgiSwapChain4;
}


// Descriptor heaps are arrays of resource views (RTV, SRV, UAV, CBV)
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
	D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	return descriptorHeap;
}

// RTV describes resource that is attached to bind slot of output merger stage
// RTV describes resource that receives the final color computed by frag shader
void UpdateRTVs(ComPtr<ID3D12Device2> device,
	ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
	auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < gNumFrames; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

		gBackBuffers[i] = backBuffer;

		rtvHandle.Offset(rtvDescriptorSize);
	}
}

// Command allocator is backing memory used by command list
ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
	D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

	return commandAllocator;
}

// Command lists are used for recording commands that get executed on the GPU
ComPtr<ID3D12GraphicsCommandList> CreatCommandList(ComPtr<ID3D12Device2> device,
	ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

	// Make sure we reset before recording commands for next frame
	ThrowIfFailed(commandList->Close());

	return commandList;
}

ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
	ComPtr<ID3D12Fence> fence;

	ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

	return fence;
}

// Event handle is used to block the CPU thread until the fence gets signaled
HANDLE CreateEventHandle()
{
	HANDLE fenceEvent;
	fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent && "Failed to create fence event.");

	return fenceEvent;
}

// Signal fence from GPU
// Gets signaled once the GPU command queue reached its point
// during execution. 
uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
	uint64_t &fenceValue)
{
	uint64_t fenceValueForSignal = ++fenceValue;
	ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

	// value that the CPU should wait for before using 
	// resources that are currently used during a frame.
	return fenceValueForSignal;
}

void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue,
	HANDLE fenceEvent, std::chrono::milliseconds duration = std::chrono::milliseconds::max())
{
	if (fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
	}
}

// Flushing makes sure that any commands that were executed 
// are finished before the next frame can be processed.
void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
	uint64_t &fenceValue, HANDLE fenceEvent)
{
	uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
	WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

void Update()
{
	static uint64_t frameCounter = 0;
	static double elapsedSeconds = 0;
	static std::chrono::high_resolution_clock clock;
	static auto t0 = clock.now();

	frameCounter++;
	auto t1 = clock.now();
	auto deltaTime = t1 - t0;

	t0 = t1;

	elapsedSeconds += deltaTime.count() * 1e-9;
	if (elapsedSeconds > 1)
	{
		char buffer[500];
		auto fps = frameCounter / elapsedSeconds;

		sprintf_s(buffer, 500, "FPS: %f\n", fps);
		OutputDebugString(buffer);

		frameCounter = 0;
		elapsedSeconds = 0.0;
	}
}

void Render()
{
	auto commandAllocator = gCommandAllocators[gCurrBackBufferIdx];
	auto backBuffer = gBackBuffers[gCurrBackBufferIdx];
	
	commandAllocator->Reset();
	
	gCommandList->Reset(commandAllocator.Get(), nullptr);

	// Clear the render target.
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			backBuffer.Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		gCommandList->ResourceBarrier(1, &barrier);

		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			gCurrBackBufferIdx, gRTVDescriptorSize);

		gCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	}

	// Present
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			backBuffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		gCommandList->ResourceBarrier(1, &barrier);

		ThrowIfFailed(gCommandList->Close());

		ID3D12CommandList* const commandLists[] = {
			gCommandList.Get()
		};
		gCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

		UINT syncInterval = gVsync ? 1 : 0;
		UINT presentFlags = gTearingSupport && !gVsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
		ThrowIfFailed(gSwapChain->Present(syncInterval, presentFlags));

		gFrameFenceValues[gCurrBackBufferIdx] = Signal(gCommandQueue, gFence, gFenceValue);

		gCurrBackBufferIdx = gSwapChain->GetCurrentBackBufferIndex();

		WaitForFenceValue(gFence, gFrameFenceValues[gCurrBackBufferIdx], gFenceEvent);
	}
}

// Triggered when launching full screen mode or resizing the current window
void Resize(uint32_t width, uint32_t height)
{
	if (gClientWidth != width || gClientHeight != height)
	{
		gClientWidth = std::max(1u, width);
		gClientHeight = std::max(1u, height);

		// Flush GPU to make sure none of the resources 
		// are being used during the resizing
		Flush(gCommandQueue, gFence, gFenceValue, gFenceEvent);

		for (int i = 0; i < gNumFrames; ++i)
		{
			// Release references to the backbuffers
			// to prevent unwanted behavior from resizing
			// the swap chain
			gBackBuffers[i].Reset();
			gFrameFenceValues[i] = gFrameFenceValues[gCurrBackBufferIdx];
		}

		// Query the current swap chain descriptor 
		// to make sure nothing changes during resizing
		DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
		ThrowIfFailed(gSwapChain->GetDesc(&swapChainDesc));
		ThrowIfFailed(gSwapChain->ResizeBuffers(gNumFrames, gClientWidth, gClientHeight,
			swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

		// Update to the most recent back buffer index
		gCurrBackBufferIdx = gSwapChain->GetCurrentBackBufferIndex();

		UpdateRTVs(gDevice, gSwapChain, gRTVDescriptorHeap);
	}
}

void SetFullscreen(bool fullscreen)
{
	if (gFullScreenMode != fullscreen)
	{
		gFullScreenMode = fullscreen;

		if (gFullScreenMode)
		{
			// Store window dimensions so that you can
			// go back to this size when getting out
			// of fullscreen mode
			::GetWindowRect(gHWnd, &gWindowRect);

			// Change to borderless
			UINT windowStyle = WS_OVERLAPPEDWINDOW & 
				~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

			::SetWindowLongW(gHWnd, GWL_STYLE, windowStyle);

			// Query the name of the nearest display device for the window
			// Required for fullscreen multi-monitor setups
			HMONITOR hMonitor = ::MonitorFromWindow(gHWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEX monitorInfo = {};
			monitorInfo.cbSize = sizeof(MONITORINFOEX);
			::GetMonitorInfo(hMonitor, &monitorInfo);

			// Change position and make sure the window is visible
			::SetWindowPos(
				gHWnd,		// handle to window 
				HWND_TOP,	// place order (top of the window stack)
				monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			::ShowWindow(gHWnd, SW_MAXIMIZE);
		}
		else
		{
			// Change back to original and restore
			::SetWindowLong(gHWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

			::SetWindowPos(
				gHWnd, 
				HWND_NOTOPMOST,
				gWindowRect.left,
				gWindowRect.top,
				gWindowRect.right - gWindowRect.left,
				gWindowRect.bottom - gWindowRect.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			::ShowWindow(gHWnd, SW_NORMAL);
		}
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (gIsInitialized)
	{
		switch (message)
		{
		case WM_PAINT:
			Update();
			Render();
			break;
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		{
			bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

			switch (wParam)
			{
			case 'V':
				gVsync = !gVsync;
				break;
			case VK_ESCAPE:
				::PostQuitMessage(0);
				break;
			case VK_RETURN:
				if (alt)
				{
			case VK_F11:
				SetFullscreen(!gFullScreenMode);
				}
				break;
			}
		}
			break;
		case WM_SYSCHAR:
			break;
		case WM_SIZE:
		{
			RECT clientRect = {};
			::GetClientRect(gHWnd, &clientRect);

			int w = clientRect.right - clientRect.left;
			int h = clientRect.bottom - clientRect.top;

			Resize(w, h);
		}
			break;
		case WM_DESTROY:
			::PostQuitMessage(0);
			break;
		default:
			return ::DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}
	else
	{
		return ::DefWindowProcW(hwnd, message, wParam, lParam);
	}

	return 0;
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	// 100% scaling
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	const wchar_t* windowClassName = L"D3D12";
	ParseCommandLineArguments();

	EnabelDebugLayer();

	//gTearingSupport = CheckTearingSupport();

	RegisterWindowClass(hInstance, windowClassName);
	gHWnd = CreateWindow(windowClassName, hInstance, L"LearningD3D12",
		gClientWidth, gClientHeight);

	// Initialize the global window rect variable.
	::GetWindowRect(gHWnd, &gWindowRect);

	ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(gUseWarp);
 
	gDevice = CreateDevice(dxgiAdapter4);
 
	gCommandQueue = CreateCommandQueue(gDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);
 
	gSwapChain = CreateSwapChain(gHWnd, gCommandQueue,
		gClientWidth, gClientHeight, gNumFrames);
 
	gCurrBackBufferIdx = gSwapChain->GetCurrentBackBufferIndex();
 
	gRTVDescriptorHeap = CreateDescriptorHeap(gDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, gNumFrames);
	gRTVDescriptorSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 
	UpdateRTVs(gDevice, gSwapChain, gRTVDescriptorHeap);

	for (int i = 0; i < gNumFrames; ++i)
	{
		gCommandAllocators[i] = CreateCommandAllocator(gDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);
	}
	gCommandList = CreatCommandList(gDevice,
		gCommandAllocators[gCurrBackBufferIdx], D3D12_COMMAND_LIST_TYPE_DIRECT);

	gFence = CreateFence(gDevice);
	gFenceEvent = CreateEventHandle();

	gIsInitialized = true;

	::ShowWindow(gHWnd, SW_SHOW);

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}

	// Make sure the command queue has finished all commands before closing.
	Flush(gCommandQueue, gFence, gFenceValue, gFenceEvent);

	::CloseHandle(gFenceEvent);

	return 0;
}