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

	int TimeToFrame(double time) const;

	HRESULT Play();
	HRESULT PlayRange(double start, double end);
	HRESULT Pause();

	bool	IsFocused() const { return m_focused; };

	double	GetLength() const;
	double	GetTime();
	HRESULT	SetTime(double time);

	ULONGLONG GetFrames() const;
	ULONGLONG GetFrame() const;
	HRESULT SetFrame(ULONGLONG frame);

	BOOL    HasVideo() const;

protected:
	virtual BOOL CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam);

private:
	const int mProgressMax = 100000000;
	struct {
		HWND mPlaybackSlider;
		HWND mOpenProgress;
		HWND mOpenCancel;

		void showOpeningWindows(bool show) {
			ShowWindow(mPlaybackSlider, !show ? SW_SHOW : SW_HIDE);
			ShowWindow(mOpenProgress, show ? SW_SHOW : SW_HIDE);
			ShowWindow(mOpenCancel, show ? SW_SHOW : SW_HIDE);
		}
	} mControls;

	RECT mVideoRect;
	BITMAPINFO mVideoBitmapInfo = { 0 };
	long m_clientTop;
	bool m_focused;

	bool				mFFLoadCancel;
	HANDLE				mFFLoader = INVALID_HANDLE_VALUE;
	FFMS_Indexer		*mFFIndexer;
	FFMS_VideoSource	*mFFV;
	FFMS_AudioSource	*mFFA;
	const FFMS_Frame	*mFrame;
	FFMS_Track			*mVideoTrack;
	const FFMS_VideoProperties	*mFFVP;
	const FFMS_AudioProperties	*mFFAP;

	WAVEFORMATEX		pwfx = { 0 };
	IMMDevice			*pDevice = NULL;
	IAudioClient		*pAudioClient = NULL;
	IAudioRenderClient	*pRenderClient = NULL;
	UINT64				mPlayStartPos;
	int					mVideoEndFrameIndex;
	UINT64				mAudioStartFrameIndex, mAudioEndFrameIndex;
	int					mPlayStartTime;
	CRITICAL_SECTION	mDecoderSync;
	int					mVideoFrameIndex;
	UINT64				mAudioFrameIndex;
	HANDLE				mDecodeVideoWait;
	HANDLE				mDecodeAudioWait;
	HANDLE				mRendererVideo = INVALID_HANDLE_VALUE;
	HANDLE				mRendererAudio = INVALID_HANDLE_VALUE;

	void ResetRendererSync();

	int FFMS_CC FFOpenCallbackInternal(int64_t Current, int64_t Total, UINT32 &lastCall);
	static int FFMS_CC FFOpenCallbackExternal(int64_t Current, int64_t Total, void *ICPrivate);
	void OpenFileInternal(TCHAR *u16, char *u8, TCHAR *iu16, char *iu8);
	static DWORD WINAPI OpenFileExternal(PVOID ptr);
	void RenderVideoInternal();
	static DWORD WINAPI RenderVideoExternal(PVOID ptr);
	void RenderAudioInternal();
	static DWORD WINAPI RenderAudioExternal(PVOID ptr);

	PlaybackState   mState = STATE_CLOSED;
};

