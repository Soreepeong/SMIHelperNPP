#include "DockedPlayer.h"
#include "PluginInterface.h"
#include "VideoRenderers.h"
#include <Commctrl.h>
#define TRACKBAR_TIMER 10000

extern NppData nppData;

DockedPlayer::DockedPlayer() :
	DockingDlgInterface(IDD_PLAYER),
	m_state(STATE_NO_GRAPH),
	m_pGraph(NULL),
	m_pControl(NULL),
	m_pSeeking(NULL),
	m_pEvent(NULL),
	m_pVideo(NULL) {
}


DockedPlayer::~DockedPlayer() {
	TearDownGraph();
}

// https://msdn.microsoft.com/en-us/library/windows/desktop/ff625878(v=vs.85).aspx

HRESULT DockedPlayer::OpenFile(PCWSTR pszFileName) {
	IBaseFilter *pSource = NULL;

	HRESULT hr = InitializeGraph();
	if (FAILED(hr)) goto done;

	hr = m_pGraph->AddSourceFilter(pszFileName, NULL, &pSource);
	if (FAILED(hr)) goto done;

	hr = RenderStreams(pSource);

done:
	if (FAILED(hr)) 
		TearDownGraph();
	else {
		if (m_currentFile != NULL) delete m_currentFile;
		int len = wcslen(pszFileName) + 1;
		m_currentFile = new TCHAR[len];
		wcsncpy(m_currentFile, pszFileName, len);

		RECT vid;
		m_pVideo->GetRect(vid);

		isVideo = vid.right > 0;
		if (isVideo) {
			m_videoWidth = vid.right;
			m_videoHeight = vid.bottom;
		} else
			m_clientTop = 0;
		SendMessage(getHSelf(), WM_SIZE, NULL, NULL);
	}
	SafeRelease(&pSource);
	return hr;
}
HRESULT DockedPlayer::HandleGraphEvent(GraphEventFN pfnOnGraphEvent) {
	if (!m_pEvent)
		return E_UNEXPECTED;

	long evCode = 0;
	LONG_PTR param1 = 0, param2 = 0;

	HRESULT hr = S_OK;

	while (SUCCEEDED(m_pEvent->GetEvent(&evCode, &param1, &param2, 0))) {
		pfnOnGraphEvent(getHSelf(), evCode, param1, param2);
		hr = m_pEvent->FreeEventParams(evCode, param1, param2);
		if (FAILED(hr)) {
			break;
		}
	}
	return hr;
}

HRESULT DockedPlayer::Play() {
	if (m_state != STATE_PAUSED && m_state != STATE_STOPPED)
		return VFW_E_WRONG_STATE;

	HRESULT hr = m_pControl->Run();
	if (SUCCEEDED(hr)) {
		m_state = STATE_RUNNING;
		SendMessage(m_slider, TBM_SETPOS, true, (int)(GetTime()*100000000. / GetLength()));
		SetTimer(getHSelf(), TRACKBAR_TIMER, 250, NULL);
	}
	return hr;
}

HRESULT DockedPlayer::Pause() {
	if (m_state != STATE_RUNNING)
		return VFW_E_WRONG_STATE;

	HRESULT hr = m_pControl->Pause();
	KillTimer(getHSelf(), TRACKBAR_TIMER);
	if (SUCCEEDED(hr))
		m_state = STATE_PAUSED;
	return hr;
}

HRESULT DockedPlayer::Stop() {
	if (m_state != STATE_RUNNING && m_state != STATE_PAUSED)
		return VFW_E_WRONG_STATE;

	HRESULT hr = m_pControl->Stop();
	KillTimer(getHSelf(), TRACKBAR_TIMER);
	if (SUCCEEDED(hr))
		m_state = STATE_STOPPED;
	return hr;
}

LONGLONG DockedPlayer::GetLength() const {
	LONGLONG res, u;
	if (m_state == STATE_NO_GRAPH)
		return -1;
	if (SUCCEEDED(m_pSeeking->GetAvailable(&u, &res)))
		return res / 10 / 1000;
	return -1;
}

HRESULT DockedPlayer::SetTime(LONGLONG time) {
	if (m_state == STATE_NO_GRAPH)
		return VFW_E_WRONG_STATE;
	SendMessage(m_slider, TBM_SETPOS, true, (int)(time*100000000. / GetLength()));
	time *= 10 * 1000; // 100 ns -> 1 ms
	return m_pSeeking->SetPositions(&time, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
}

LONGLONG DockedPlayer::GetTime() {
	LONGLONG cur;
	if (m_state == STATE_NO_GRAPH)
		return -1;
	if (SUCCEEDED(m_pSeeking->GetCurrentPosition(&cur)))
		return cur / 10 / 1000;
	return -1;
}


// EVR/VMR functionality

BOOL DockedPlayer::HasVideo() const {
	return (m_pVideo && m_pVideo->HasVideo());
}

// Sets the destination rectangle for the video.

HRESULT DockedPlayer::UpdateVideoWindow(const LPRECT prc) {
	if (m_pVideo) {
		return m_pVideo->UpdateVideoWindow(getHSelf(), prc);
	} else {
		return S_OK;
	}
}

// Repaints the video. Call this method when the application receives WM_PAINT.

HRESULT DockedPlayer::Repaint(HDC hdc) {
	if (m_pVideo) {
		return m_pVideo->Repaint(getHSelf(), hdc);
	} else {
		return S_OK;
	}
}


// Notifies the video renderer that the display mode changed.
//
// Call this method when the application receives WM_DISPLAYCHANGE.

HRESULT DockedPlayer::DisplayModeChanged() {
	if (m_pVideo) {
		return m_pVideo->DisplayModeChanged();
	} else {
		return S_OK;
	}
}


// Graph building

// Create a new filter graph. 
HRESULT DockedPlayer::InitializeGraph() {
	TearDownGraph();

	// Create the Filter Graph Manager.
	HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pGraph));
	if (FAILED(hr)) goto done;

	hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&m_pControl));
	if (FAILED(hr)) goto done;

	hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&m_pEvent));
	if (FAILED(hr)) goto done;

	hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&m_pSeeking));
	if (FAILED(hr)) goto done;

	// Set up event notification.
	hr = m_pEvent->SetNotifyWindow((OAHWND)getHSelf(), WM_GRAPH_EVENT, NULL);
	if (FAILED(hr)) goto done;

	m_state = STATE_STOPPED;

done:
	return hr;
}

void DockedPlayer::TearDownGraph() {
	// Stop sending event messages
	if (m_pEvent) {
		m_pEvent->SetNotifyWindow((OAHWND)NULL, NULL, NULL);
	}

	SafeRelease(&m_pGraph);
	SafeRelease(&m_pControl);
	SafeRelease(&m_pEvent);
	SafeRelease(&m_pSeeking);

	if (m_pVideo != NULL) {
		delete m_pVideo;
		m_pVideo = NULL;
	}

	m_state = STATE_NO_GRAPH;
	if (m_currentFile != NULL) {
		delete m_currentFile;
		m_currentFile = NULL;
	}
	KillTimer(getHSelf(), TRACKBAR_TIMER);
}


HRESULT DockedPlayer::CreateVideoRenderer() {
	HRESULT hr = E_FAIL;

	enum { Try_EVR, Try_VMR9, Try_VMR7 };

	for (DWORD i = Try_EVR; i <= Try_VMR7; i++) {
		switch (i) {
			case Try_EVR:
				m_pVideo = new (std::nothrow) CEVR();
				break;

			case Try_VMR9:
				m_pVideo = new (std::nothrow) CVMR9();
				break;

			case Try_VMR7:
				m_pVideo = new (std::nothrow) CVMR7();
				break;
		}

		if (m_pVideo == NULL) {
			hr = E_OUTOFMEMORY;
			break;
		}

		hr = m_pVideo->AddToGraph(m_pGraph, getHSelf());
		if (SUCCEEDED(hr)) {
			break;
		}

		delete m_pVideo;
		m_pVideo = NULL;
	}
	return hr;
}


// Render the streams from a source filter. 

HRESULT DockedPlayer::RenderStreams(IBaseFilter *pSource) {
	BOOL bRenderedAnyPin = FALSE;

	IFilterGraph2 *pGraph2 = NULL;
	IEnumPins *pEnum = NULL;
	IBaseFilter *pAudioRenderer = NULL;
	HRESULT hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&pGraph2));
	if (FAILED(hr)) goto done;

	// Add the video renderer to the graph
	hr = CreateVideoRenderer();
	if (FAILED(hr)) goto done;

	// Add the DSound Renderer to the graph.
	hr = AddFilterByCLSID(m_pGraph, CLSID_DSoundRender,
		&pAudioRenderer, L"Audio Renderer");
	if (FAILED(hr)) goto done;

	// Enumerate the pins on the source filter.
	hr = pSource->EnumPins(&pEnum);
	if (FAILED(hr)) goto done;

	// Loop through all the pins
	IPin *pPin;
	while (S_OK == pEnum->Next(1, &pPin, NULL)) {
		// Try to render this pin. 
		// It's OK if we fail some pins, if at least one pin renders.
		HRESULT hr2 = pGraph2->RenderEx(pPin, AM_RENDEREX_RENDERTOEXISTINGRENDERERS, NULL);

		pPin->Release();
		if (SUCCEEDED(hr2)) {
			bRenderedAnyPin = TRUE;
		}
	}

	hr = m_pVideo->FinalizeGraph(m_pGraph);
	if (FAILED(hr)) goto done;

	// Remove the audio renderer, if not used.
	BOOL bRemoved;
	hr = RemoveUnconnectedRenderer(m_pGraph, pAudioRenderer, &bRemoved);

done:
	SafeRelease(&pEnum);
	SafeRelease(&pAudioRenderer);
	SafeRelease(&pGraph2);

	// If we succeeded to this point, make sure we rendered at least one 
	// stream.
	if (SUCCEEDED(hr)) {
		if (!bRenderedAnyPin) {
			hr = VFW_E_CANNOT_RENDER;
		}
	}
	return hr;
}

BOOL CALLBACK DockedPlayer::run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG: {
			m_slider = GetDlgItem(getHSelf(), IDC_PLAYER_NAVIGATE);
			SendMessage(m_slider, TBM_SETRANGEMIN, TRUE, 0);
			SendMessage(m_slider, TBM_SETRANGEMAX, TRUE, 100000000);
			break;
		}
		case WM_HSCROLL: {
			double k = SendMessage(m_slider, TBM_GETPOS, NULL, NULL) / 100000000.;
			SetTime((LONGLONG)(GetLength() * k));
			break;
		}
		case WM_TIMER: {
			if (wParam == TRACKBAR_TIMER) {
				SendMessage(m_slider, TBM_SETPOS, true, (int)(GetTime()*100000000. / GetLength()));
			}
			break;
		}
		case WM_SIZE: {
			RECT r, r2;
			this->getClientRect(r);
			if (isVideo) {
				if (m_videoWidth != 0) {
					r.right -= r.left; r.left = 0;
					r.top = 0;
					r.bottom = r.right * m_videoHeight / m_videoWidth;
					m_clientTop = r.bottom;
				}
				UpdateVideoWindow(&r);
			} else
				m_clientTop = 0;
			GetWindowRect(m_slider, &r2);
			SetWindowPos(m_slider, NULL, 0, m_clientTop, r.right, r2.bottom - r2.top, SWP_NOACTIVATE);
			break;
		}
		case WM_ERASEBKGND: {
			RECT r;
			HDC hdc = (HDC)wParam;
			this->getClientRect(r);
			r.top = m_clientTop;
			FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1));
			return true;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			SetFocus(getHSelf());
			break;
		case WM_NOTIFY: {
			bool nf = GetFocus() == getHSelf() || GetFocus() == m_slider;
			if (m_focused == nf)
				break;
			m_focused = nf;
			RECT rc;
			GetClientRect(m_slider, &rc);
			rc.left = 10;
			rc.right -= 10;
			rc.top = m_clientTop + rc.bottom + 10;
			rc.bottom = rc.top + 600;
			InvalidateRect(getHSelf(), &rc, true);
			break;
		}
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc;

			hdc = BeginPaint(getHSelf(), &ps);

			if (State() != STATE_NO_GRAPH && HasVideo()) {
				// The player has video, so ask the player to repaint. 
				Repaint(hdc);
			} else {
				FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
			}
			if (m_focused) {
				RECT rc;
				const TCHAR *msg = L""
					"Left: -3 sec\n"
					"Right: +3 sec\n"
					"Space: Play / Pause\n"
					"Z, Num0: Undo\n"
					"X, Up: Insert timecode\n"
					"C, Down: Insert stop timecode";
				GetClientRect(m_slider, &rc);
				rc.left = 10;
				rc.right -= 10;
				rc.top = m_clientTop + rc.bottom + 10;
				rc.bottom = rc.top + 600;
				DrawText(hdc, msg, wcslen(msg), &rc, DT_LEFT | DT_EXTERNALLEADING | DT_WORDBREAK);
			}

			EndPaint(getHSelf(), &ps);
			return true;
		}
		case WM_DESTROY: {
			TearDownGraph();
			break;
		}
		case WM_GRAPH_EVENT: {
			// TODO ??
			break;
		}
	}
	return DockingDlgInterface::run_dlgProc(message, wParam, lParam);
}

