#pragma once
#include "DockingFeature/DockingDlgInterface.h"
#include "resource.h"
#include "ffms2/ffms.h"
#include<Audioclient.h>
#include<Mmdeviceapi.h>
#include<Endpointvolume.h>

class CVideoRenderer;

typedef void (CALLBACK *GraphEventFN)(HWND hwnd, long eventCode, LONG_PTR param1, LONG_PTR param2);


// https://msdn.microsoft.com/en-us/library/windows/desktop/ff625879(v=vs.85).aspx

enum PlaybackState
{
	STATE_CLOSED,
	STATE_OPENING,
	STATE_RUNNING,
	STATE_PAUSED
};

class DockedPlayer: public DockingDlgInterface
{
public:
	DockedPlayer();
	~DockedPlayer();

	void setParent(HWND parent2set) {
		_hParent = parent2set;
	};


	PlaybackState State() const { return mState; }

	HRESULT OpenFile(PCWSTR pszFileName);
	void    CloseFile();

	HRESULT Play();
	HRESULT Pause();

	bool	IsFocused() const { return m_focused; };

	double	GetLength() const;
	double	GetTime();
	HRESULT	SetTime(double time);

	BOOL    HasVideo() const;

protected:
	virtual BOOL CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND m_slider;

	RECT mVideoRect;
	BITMAPINFO mVideoBitmapInfo = { 0 };
	long m_clientTop;
	bool m_focused;

	HANDLE				mFFLoader = INVALID_HANDLE_VALUE;
	FFMS_Indexer		*mFFIndexer;
	FFMS_VideoSource	*mFFV;
	FFMS_AudioSource	*mFFA;
	int					mFrameIndex;
	const FFMS_Frame	*mFrame;
	FFMS_Track			*mVideoTrack;
	const FFMS_FrameInfo	*mFrameInfo;
	const FFMS_VideoProperties	*mFFVP;
	const FFMS_AudioProperties	*mFFAP;

	WAVEFORMATEX		pwfx = { 0 };
	IMMDevice			*pDevice = NULL;
	IAudioClient		*pAudioClient = NULL;
	IAudioRenderClient	*pRenderClient = NULL;
	bool				mPlayResetSync;
	CRITICAL_SECTION	mDecoderSync;
	HANDLE				mDecodeWait;
	HANDLE				mRenderer = INVALID_HANDLE_VALUE;

	void OpenFileInternal(TCHAR *u16, char *u8, TCHAR *iu16, char *iu8);
	static DWORD WINAPI OpenFileExternal(PVOID ptr);
	void RendererInternal();
	static DWORD WINAPI RendererExternal(PVOID ptr);

	PlaybackState   mState = STATE_CLOSED;
};

