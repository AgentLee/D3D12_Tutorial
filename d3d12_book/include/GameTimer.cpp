#include <windows.h>
#include "GameTimer.h"

// Queries the frequency of the performance counter
GameTimer::GameTimer() :
	m_secondsPerCount(0.0), m_deltaTime(-1.0), m_baseTime(0),
	m_pausedTime(0), m_prevTime(0), m_currTime(0), m_stopped(false)
{
	__int64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);

	m_secondsPerCount = 1.0 / (double)countsPerSec;
}

// Total time since Reset()
float GameTimer::TotalTime() const
{
	if (m_stopped)
	{
		return (float)(((m_stopTime - m_pausedTime) - m_baseTime) * m_secondsPerCount);
	}
	else
	{
		return (float)(((m_currTime - m_pausedTime) - m_baseTime) * m_secondsPerCount);
	}

}

float GameTimer::DeltaTime() const
{
	return (float)m_deltaTime;
}

void GameTimer::Reset()
{
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	m_baseTime = currTime;
	m_prevTime = currTime;
	m_stopTime = 0;
	m_stopped = false;
}

void GameTimer::Start()
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);

	if (m_stopped)
	{
		m_pausedTime += (startTime - m_stopTime);

		m_prevTime = startTime;

		m_stopTime = 0;
		m_stopped = false;
	}
}

void GameTimer::Stop()
{
	if (!m_stopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

		m_stopTime = currTime;
		m_stopped = true;
	}
}

// Calculate the amount of time between frames
// Gets called in the message loop of the main application
void GameTimer::Tick()
{
	if (m_stopped)
	{
		m_deltaTime = 0;
		return;
	}

	// Get current time on current frame
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	m_currTime = currTime;

	// Difference between this and previous frame
	m_deltaTime = (m_currTime - m_prevTime) * m_secondsPerCount;

	// Update for next frame
	m_prevTime = m_currTime;

	// Account for negative delta which can happen
	// wif the computer goes to power save mode or if
	// the computer shuffles the application to another proc
	if (m_deltaTime < 0)
	{
		m_deltaTime = 0;
	}
}
