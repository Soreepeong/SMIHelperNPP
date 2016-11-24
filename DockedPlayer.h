#pragma once
#include "DockingFeature/DockingDlgInterface.h"
#include "resource.h"
#include <dshow.h>

class CVideoRenderer;

typedef void (CALLBACK *GraphEventFN)(HWND hwnd, long eventCode, LONG_PTR param1, LONG_PTR param2);


// https://msdn.microsoft.com/en-us/library/windows/desktop/ff625879(v=vs.85).aspx

enum PlaybackState
{
	STATE_NO_GRAPH,
	STATE_RUNNING,
	STATE_PAUSED,
	STATE_STOPPED,
};

class DockedPlayer: public DockingDlgInterface
{
public:
	DockedPlayer();
	~DockedPlayer();

	void setParent(HWND parent2set) {
		_hParent = parent2set;
	};


	PlaybackState State() const { return m_state; }

	HRESULT OpenFile(PCWSTR pszFileName);
	void    TearDownGraph();
	const TCHAR* GetFileName() const { return m_currentFile; };

	HRESULT Play();
	HRESULT Pause();
	HRESULT Stop();

	bool	IsFocused() const { return m_focused; };

	LONGLONG	GetLength() const;
	LONGLONG	GetTime();
	HRESULT		SetTime(LONGLONG time);

	BOOL    HasVideo() const;
	HRESULT UpdateVideoWindow(const LPRECT prc);
	HRESULT Repaint(HDC hdc);
	HRESULT DisplayModeChanged();

	HRESULT HandleGraphEvent(GraphEventFN pfnOnGraphEvent);

protected:
	virtual BOOL CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND m_slider;

	long m_videoWidth, m_videoHeight;
	long m_clientTop;
	bool isVideo;
	bool m_focused;

	HRESULT InitializeGraph();
	HRESULT CreateVideoRenderer();
	HRESULT RenderStreams(IBaseFilter *pSource);

	PlaybackState   m_state;

	TCHAR			*m_currentFile;
	IGraphBuilder   *m_pGraph;
	IMediaControl   *m_pControl;
	IMediaSeeking	*m_pSeeking;
	IMediaEventEx   *m_pEvent;
	CVideoRenderer  *m_pVideo;
};

