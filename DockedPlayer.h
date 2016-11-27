#pragma once
#include "DockingFeature/DockingDlgInterface.h"
#include "resource.h"
#include "ffms2/ffms.h"
#include <Mmsystem.h>
#include <Mmreg.h>
#include <map>
#include <string>
#include <vector>

enum PlaybackState {
	STATE_CLOSED,
	STATE_OPENING,
	STATE_RUNNING,
	STATE_PAUSED,
	STATE_ERROR
};

class DockedPlayer : public DockingDlgInterface {
public:
	DockedPlayer();
	~DockedPlayer();

	class Media {
	public:
		Media(TCHAR *filename, DockedPlayer *player);
		~Media();
		void CancelOpening();

		PlaybackState State() const { return mState; }
		const char* GetErrorMsg() const { return mErrorMsg; }

		int TimeToFrame(double time) const;

		HRESULT Play();
		HRESULT PlayRange(double start, double end);
		HRESULT Pause();

		BOOL    HasVideo() const;
		const	BITMAPINFO& GetBitmapInfo() const { return mVideoBitmapInfo; };
		const	FFMS_Frame*	GetFrame() const { return mFrame; };

		double	GetLength() const;
		double	GetTime();
		HRESULT	SetTime(double time, bool updateTrackbarPosition = true);

		ULONGLONG GetFrameCount() const;
		ULONGLONG GetFrameIndex() const;
		HRESULT SetFrameIndex(ULONGLONG frame, bool updateTrackbarPosition = true);

	private:
		DockedPlayer *mDockedPlayer;
		BITMAPINFO mVideoBitmapInfo = { 0 };

		char mErrorMsg[1024];

		bool				mFFLoadCancel = false;
		HANDLE				mFFLoader = INVALID_HANDLE_VALUE;
		FFMS_Indexer		*mFFIndexer = NULL;
		FFMS_VideoSource	*mFFV = NULL;
		FFMS_AudioSource	*mFFA = NULL;
		const FFMS_Frame	*mFrame = NULL;
		FFMS_Track			*mVideoTrack = NULL;
		const FFMS_VideoProperties	*mFFVP = NULL;
		const FFMS_AudioProperties	*mFFAP = NULL;

		WAVEFORMATEX		mWaveFormat = { 0 };
		HWAVEOUT			mWaveOut = NULL;
		UINT64				mPlayStartPos = NULL;
		int					mVideoEndFrameIndex = NULL;
		UINT64				mAudioStartFrameIndex = NULL, mAudioEndFrameIndex = NULL;
		int					mPlayStartTime = NULL;
		CRITICAL_SECTION	mDecoderSync = { 0 };
		int					mVideoFrameIndex = NULL;
		UINT64				mAudioFrameIndex = NULL;
		HANDLE				mDecodeVideoWait = NULL;
		HANDLE				mDecodeAudioWait = NULL;
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

	void setParent(HWND parent2set) {
		_hParent = parent2set;
	};

	void OpenFile(TCHAR *pszFileName);
	Media* GetMedia();

	bool	IsFocused() const { return m_focused; };

	void	SetTab(std::wstring id);
	void	CloseTab(std::wstring id);
	std::vector<std::wstring> GetOpenIds() const;

protected:
	virtual BOOL CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam);

private:
	static const int mProgressMax = 100000000;
	struct {
		HWND mPlaybackSlider;
		HWND mOpenProgress;
		HWND mOpenCancel;

		void showOpeningWindows(bool show) {
			if (!!IsWindowVisible(mOpenProgress) == !!show)
				return;
			ShowWindow(mPlaybackSlider, !show ? SW_SHOW : SW_HIDE);
			ShowWindow(mOpenProgress, show ? SW_SHOW : SW_HIDE);
			ShowWindow(mOpenCancel, show ? SW_SHOW : SW_HIDE);
		}
	} mControls;

	std::map<std::wstring, Media*> mMedia;

	std::wstring mCurrentTab;
	RECT mVideoRect;
	bool m_focused;

	void OnProgressCallback(DockedPlayer::Media *media, bool visible, int progress);

	void OnSizeRequested(DockedPlayer::Media *media);
	void UpdateFrame(DockedPlayer::Media *media);
	void SetTrackbarPosition(DockedPlayer::Media *media, int position);
	void StartTrackbarUpdate(DockedPlayer::Media *media);
	void StopTrackbarUpdate(DockedPlayer::Media *media);
};