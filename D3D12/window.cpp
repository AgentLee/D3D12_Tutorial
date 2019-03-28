#include "window.h"

Window::Window(HWND hWnd, const std::wstring &windowName, int w, int h, bool vSync) :
	m_hWnd(hWnd),
	m_windowName(windowName),
	m_clientWidth(w),
	m_clientHeight(h),
	m_vSync(vSync),
	m_fullScreenMode(false),
	m_frameCounter(0)
{
	
}

HWND Window::CreateWindow(const wchar_t *windowClassName, HINSTANCE hInst, const wchar_t *windowName, int width, int height, bool vSync)
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
		windowName,
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
