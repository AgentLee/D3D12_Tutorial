#pragma once

#include "includes.h"
#include "application.h"

class Window
{
public:
	Window(HWND hWnd, const std::wstring &windowName, int w, int h, bool vSync);
	HWND CreateWindow(const wchar_t *windowClassName, HINSTANCE hInst, const wchar_t *windowName, int width, int height, bool vSync);

private:
	HWND m_hWnd;
	
	std::wstring m_windowName;
	
	int m_clientWidth;
	int m_clientHeight;
	bool m_vSync;
	bool m_fullScreenMode;

	int m_frameCounter;
};