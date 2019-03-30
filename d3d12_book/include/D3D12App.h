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

#include <d3d12.h>
#include <dxgi1_6.h>			// low level task management
#include <d3dcompiler.h>		// all the header files to compile shaders at runtime
#include <DirectXMath.h>
#include <d3dx12.h>				// useful classes that can simplify function calling

#include <algorithm>
#include <cassert>
#include <chrono>

#include "GameTimer.h"

class D3D12App
{
protected:
	D3D12App(HINSTANCE hInstance);
	D3D12App(const D3D12App &rhs) = delete;
	D3D12App &operator=(const D3D12App &rhs) = delete;
	virtual ~D3D12App();

public:
	static D3D12App *GetApp();

	HINSTANCE AppInst() const;
	HWND MainWnd() const;
	float AspectRatio() const;

	bool Get4xMSSAState() const;
	void Set4xMSSASate(bool value);

	int Run();

	virtual bool Initialize();
	virtual LRESULT MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
	virtual void CreateDescriptorHeaps();
	virtual void OnResize();
	virtual void Update(const GameTimer &timer) = 0;
	virtual void Draw(const GameTimer &timer) = 0;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) {}
	virtual void OnMouseUp(WPARAM btnState, int x, int y) {}
	virtual void OnMouseMove(WPARAM btnState, int x, int y) {}

	bool InitMainWindow();
	bool InitDirect3D();
	void CreateCommandObjects();
	void CreateSwapChain();
	void FlushCommandQueue();

	ID3D12Resource *CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	void FPS();

	void LogAdapters();
	void LogAdapterOutputs(IDXGIAdapter *adapter);
	void LogOutputDisplayModes(IDXGIOutput *output, DXGI_FORMAT format);

protected:
	static D3D12App *m_app;

	HINSTANCE m_hInst = nullptr;
	HWND m_hWnd = nullptr;

	bool m_appPaused = false;
	bool m_appMinimized = false;
	bool m_appMaximized = false;
	bool m_appResizing = false;
	bool m_appFullscreen = false;

	bool m_4xMSSAState = false;
	UINT m_4xMSSAQuality = 0;

	GameTimer m_timer;

	Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgiFactory;
	Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> m_d3Device;
	
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_currFence = 0;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

	static const int NumSwapChainBuffers = 2;
	int m_currBackBuffer = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_swapChainBuffer[NumSwapChainBuffers];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

	D3D12_VIEWPORT m_screenViewport;
	D3D12_RECT m_scissorRect;

	UINT m_rtvDescriptorSize = 0;
	UINT m_dsvDescriptorSize = 0;
	UINT m_cbvSrvUavDescriptorSize = 0;

	// Derived class should set these in derived constructor to customize starting values.
	std::wstring m_mainWndCaption = L"d3d App";
	D3D_DRIVER_TYPE m_d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT m_depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int m_clientWidth = 800;
	int m_clientHeight = 600;
};