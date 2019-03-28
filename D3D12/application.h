#pragma once

#include "includes.h"

// Initialize the application and the D3D12 device and command queues.
// Create the window and its instances. The Application class
// should be the only one to mess with the Window class.
// Run() runs the game and executes the message loop.
// Quit() quits the application.

class Application
{
public:
	Application();	
	Application(HINSTANCE hInst, const wchar_t *windowClassName);

	void EnableDebugLayer();
	void RegisterWindowClass();
	ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	ComPtr<ID3D12Device2> CreateDevice();
	ComPtr<ID3D12CommandQueue> CreateCommandQueue(D3D12_COMMAND_LIST_TYPE type);
	bool CheckTearingSupport();

	bool initialized;

	// MOVE TO WINDOW.H
	HWND CreateWindow();
	ComPtr<IDXGISwapChain4> CreateSwapChain();

private:
	HINSTANCE m_hInst;
	bool m_tearingSupport;
	const wchar_t *m_windowClassName;
	ComPtr<IDXGIAdapter4> m_dxgiAdapter;
	ComPtr<ID3D12Device2> m_d3d12Device;

	ComPtr<ID3D12CommandQueue> m_commandQueue;

	// MOVE TO WINDOW.H
	int m_numBuffers;
	int m_clientWidth, m_clientHeight;
	HWND m_hWnd;
	RECT m_windowRect;
	wchar_t *m_windowTitle;

	ComPtr<IDXGISwapChain4> m_swapChain;
};