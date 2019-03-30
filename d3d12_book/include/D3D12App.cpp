#include "D3D12App.h"
#include <windowsx.h>


using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return D3D12App::GetApp()->MsgProc(hWnd, msg, wParam, lParam);
}

D3D12App *D3D12App::m_app = nullptr;
D3D12App *D3D12App::GetApp()
{
	return m_app;
}

D3D12App::D3D12App(HINSTANCE hInstance) : m_hInst(hInstance)
{
	assert(m_app == nullptr);
	m_app = this;
}

D3D12App::~D3D12App()
{
	if (m_d3Device != nullptr)
	{
		FlushCommandQueue();
	}
}

HINSTANCE D3D12App::AppInst() const
{
	return m_hInst;
}

HWND D3D12App::MainWnd() const
{
	return m_hWnd;
}

float D3D12App::AspectRatio() const
{
	return static_cast<float>(m_clientWidth / m_clientHeight);
}

bool D3D12App::Get4xMSSAState() const
{
	return m_4xMSSAState;
}

void D3D12App::Set4xMSSASate(bool value)
{
	if (m_4xMSSAState != value)
	{
		m_4xMSSAState - value;

		// Recreate swapchain and buffers with new multisample settings
		CreateSwapChain();
		OnResize();
	}
}

int D3D12App::Run()
{
	MSG msg = { 0 };

	m_timer.Reset();

	while (msg.message != WM_QUIT)
	{
		// Process messages if they exist
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			m_timer.Tick();

			if (!m_appPaused)
			{
				FPS();
				Update(m_timer);
				Draw(m_timer);
			}
			else
			{
				Sleep(100);
			}
		}
	}

	return (int)msg.wParam;
}

