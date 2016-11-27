#include "DockedPlayer.h"
#include "PluginInterface.h"
#include <Commctrl.h>
#include <Mmsystem.h>
#define TRACKBAR_TIMER 10000

extern NppData nppData;
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

DockedPlayer::DockedPlayer() :
	DockingDlgInterface(IDD_PLAYER){
	FFMS_Init(0, 0);
	mDecodeAudioWait = CreateEvent(NULL, false, false, NULL);
	mDecodeVideoWait = CreateEvent(NULL, false, false, NULL);
	InitializeCriticalSection(&mDecoderSync);
	mVideoBitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	mVideoBitmapInfo.bmiHeader.biBitCount = 32;
	mVideoBitmapInfo.bmiHeader.biPlanes = 1;
	mVideoBitmapInfo.bmiHeader.biCompression = BI_RGB;
	mVideoBitmapInfo.bmiHeader.biClrUsed = 0;
}


DockedPlayer::~DockedPlayer() {
	CloseFile();
	CloseHandle(mDecodeAudioWait);
	CloseHandle(mDecodeVideoWait);
	DeleteCriticalSection(&mDecoderSync);
}

void DockedPlayer::OpenFileInternal(TCHAR *u16, char *u8, TCHAR *iu16, char *iu8) {
	FFMS_Index			*index = NULL;
	char				errmsg[1024] = { 0 };
	FFMS_ErrorInfo		errinfo = { FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(errmsg), errmsg };
	DWORD flags = 0;
	struct {
		DockedPlayer *player;
		UINT32 lastCall;
	} loaderCallbackData = { this, 0 };

	mState = PlaybackState::STATE_OPENING;
	SendMessage(mControls.mOpenProgress, PBM_SETPOS, 0, 0);
	mControls.showOpeningWindows(true);

	if (PathFileExists(iu16))
		index = FFMS_ReadIndex(iu8, &errinfo);
	if (index == NULL) {
		mFFLoadCancel = false;
		if ((mFFIndexer = FFMS_CreateIndexer(u8, &errinfo)) == NULL) goto done;
		FFMS_SetProgressCallback(mFFIndexer, DockedPlayer::FFOpenCallbackExternal, &loaderCallbackData);
		// FFMS_TrackTypeIndexSettings(FFMS_Indexer *Indexer, int TrackType, int Index, int Dump);
		FFMS_TrackTypeIndexSettings(mFFIndexer, FFMS_TYPE_VIDEO, true, false);
		FFMS_TrackTypeIndexSettings(mFFIndexer, FFMS_TYPE_AUDIO, true, false);
		if ((index = FFMS_DoIndexing2(mFFIndexer, FFMS_IEH_IGNORE, &errinfo)) == NULL) goto done;
		mFFIndexer = NULL;
		if ((FFMS_WriteIndex(iu8, index, &errinfo)) != 0) goto done;
	}

	int vidId = FFMS_GetFirstTrackOfType(index, FFMS_TYPE_VIDEO, &errinfo);
	if (vidId < 0 && errinfo.SubType != FFMS_ERROR_NOT_AVAILABLE) goto done;

	int audId = FFMS_GetFirstTrackOfType(index, FFMS_TYPE_AUDIO, &errinfo);
	if (audId < 0 && errinfo.SubType != FFMS_ERROR_NOT_AVAILABLE) goto done;

	if (vidId < 0 && audId < 0) goto done;

	if (vidId >= 0) {
		int pixfmts[2];
		pixfmts[0] = FFMS_GetPixFmt("bgra");
		pixfmts[1] = -1;
		mVideoFrameIndex = 0;
		if ((mFFV = FFMS_CreateVideoSource(u8, vidId, index, 1, FFMS_SEEK_NORMAL, &errinfo)) == NULL) goto done;
		mFFVP = FFMS_GetVideoProperties(mFFV);
		if ((mFrame = FFMS_GetFrame(mFFV, mVideoFrameIndex, &errinfo)) == NULL) goto done;
		mVideoTrack = FFMS_GetTrackFromVideo(mFFV);
		if ((FFMS_SetOutputFormatV2(mFFV, pixfmts,
			mFrame->ScaledWidth == -1 ? mFrame->EncodedWidth : mFrame->ScaledWidth,
			mFrame->ScaledHeight == -1 ? mFrame->EncodedHeight : mFrame->ScaledHeight,
			FFMS_RESIZER_BICUBIC, &errinfo)) != 0) goto done;
		mVideoBitmapInfo.bmiHeader.biSizeImage = 
			(mVideoBitmapInfo.bmiHeader.biWidth = mFrame->ScaledWidth == -1 ? mFrame->EncodedWidth : mFrame->ScaledWidth) * 
			(mVideoBitmapInfo.bmiHeader.biHeight = mFrame->ScaledHeight == -1 ? mFrame->EncodedHeight : mFrame->ScaledHeight) * 4;
	}
	if (audId >= 0) {
		if ((mFFA = FFMS_CreateAudioSource(u8, audId, index, FFMS_DELAY_FIRST_VIDEO_TRACK, &errinfo)) == NULL) goto done;
		mFFAP = FFMS_GetAudioProperties(mFFA);
		mWaveFormat.nBlockAlign = mFFAP->BitsPerSample * mFFAP->Channels / 8;
		mWaveFormat.nAvgBytesPerSec = mWaveFormat.nBlockAlign * mFFAP->SampleRate;
		mWaveFormat.nChannels = mFFAP->Channels;
		mWaveFormat.nSamplesPerSec = mFFAP->SampleRate;
		mWaveFormat.wBitsPerSample = mFFAP->BitsPerSample;
		mWaveFormat.wFormatTag = mFFAP->SampleFormat == FFMS_FMT_FLT || mFFAP->SampleFormat == FFMS_FMT_DBL ?
			WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;

		if (waveOutOpen(&mWaveOut,WAVE_MAPPER,&mWaveFormat,0,0,CALLBACK_NULL) != MMSYSERR_NOERROR) goto done;
	}

done:
	if(index != NULL)
		FFMS_DestroyIndex(index);
	
	CloseHandle(mFFLoader);
	mFFLoader = INVALID_HANDLE_VALUE;
	if (errinfo.ErrorType != FFMS_ERROR_SUCCESS) {
		CloseFile();
	} else {
		mState = PlaybackState::STATE_PAUSED;
		PostMessage(getHSelf(), WM_SIZE, NULL, NULL);
		mRendererAudio = CreateThread(NULL, NULL, DockedPlayer::RenderAudioExternal, (PVOID)this, NULL, NULL);
		mRendererVideo = CreateThread(NULL, NULL, DockedPlayer::RenderVideoExternal, (PVOID)this, NULL, NULL);
	}
	mControls.showOpeningWindows(false);
}
DWORD WINAPI DockedPlayer::OpenFileExternal(PVOID ptr) {
	struct _data {
		DockedPlayer *t;
		TCHAR *utf16;
		char *utf8;
		TCHAR *index_utf16;
		char *index_utf8;
	} *data = reinterpret_cast<_data*>(ptr);
	data->t->OpenFileInternal(data->utf16, data->utf8, data->index_utf16, data->index_utf8);
	delete[] data->utf16;
	delete[] data->utf8;
	delete[] data->index_utf16;
	delete[] data->index_utf8;
	delete[] data;
	return 0;
}

HRESULT DockedPlayer::OpenFile(PCWSTR pszFileName) {
	struct _data {
		DockedPlayer *t;
		TCHAR *utf16;
		char *utf8;
		TCHAR *index_utf16;
		char *index_utf8;
	} *data = new _data;
	int utf16len = wcslen(pszFileName);
	int utf8len = WideCharToMultiByte(CP_UTF8, 0, pszFileName, utf16len, NULL, 0, NULL, NULL);
	char *utf8 = new char[utf8len + 1];
	TCHAR *indexfile = new TCHAR[utf16len + 16];
	char *utf8_indexfile = new char[utf8len + 16];

	WideCharToMultiByte(CP_UTF8, 0, pszFileName, utf16len, utf8, utf8len, 0, 0);
	WideCharToMultiByte(CP_UTF8, 0, pszFileName, utf16len, utf8_indexfile, utf8len, 0, 0);
	strncpy(utf8_indexfile + utf8len, ".ffindex", 9);
	wcsncpy(indexfile, pszFileName, utf16len);
	wcsncpy(indexfile + utf16len, L".ffindex", 9);
	utf8[utf8len] = 0;

	data->t = this;
	data->utf16 = new TCHAR[utf16len + 1];
	wcsncpy(data->utf16, pszFileName, utf16len + 1);
	data->utf8 = utf8;
	data->index_utf16 = indexfile;
	data->index_utf8 = utf8_indexfile;
	mFFLoader = CreateThread(NULL, NULL, DockedPlayer::OpenFileExternal, (PVOID)data, NULL, NULL);
	return GetLastError();
}

int FFMS_CC DockedPlayer::FFOpenCallbackInternal(int64_t Current, int64_t Total, UINT32 &lastCall) {
	if (lastCall + 50 < GetTickCount()) {
		PostMessage(mControls.mOpenProgress, PBM_SETPOS, static_cast<int>((double)mProgressMax / Total * Current), 0);
		lastCall = GetTickCount();
	}
	return mFFLoadCancel;
}
int FFMS_CC DockedPlayer::FFOpenCallbackExternal(int64_t Current, int64_t Total, void *ICPrivate) {
	struct _data {
		DockedPlayer *player;
		UINT32 lastCall;
	} *data = reinterpret_cast<_data*>(ICPrivate);
	return data->player->FFOpenCallbackInternal(Current, Total, data->lastCall);
}

void DockedPlayer::CloseFile() {
	EnterCriticalSection(&mDecoderSync);
	mState = STATE_CLOSED;
	if (mFFLoader != INVALID_HANDLE_VALUE) {
		FFMS_CancelIndexing(mFFIndexer);
		if (WaitForSingleObject(mFFLoader, 1000) == WAIT_TIMEOUT)
			TerminateThread(mFFLoader, -1);
		CloseHandle(mFFLoader);
		mFFIndexer = NULL;
		mFFLoader = INVALID_HANDLE_VALUE;
	}
	if (mRendererVideo != INVALID_HANDLE_VALUE) {
		SetEvent(mDecodeVideoWait);
		WaitForSingleObject(mRendererVideo, INFINITE);
		CloseHandle(mRendererVideo);
		mRendererVideo = INVALID_HANDLE_VALUE;
	}
	if (mRendererAudio != INVALID_HANDLE_VALUE) {
		SetEvent(mDecodeAudioWait);
		WaitForSingleObject(mRendererAudio, INFINITE);
		CloseHandle(mRendererAudio);
		mRendererAudio = INVALID_HANDLE_VALUE;
	}
	if (mFFV != NULL)
		FFMS_DestroyVideoSource(mFFV);
	if (mFFA != NULL)
		FFMS_DestroyAudioSource(mFFA);
	if (mWaveOut != NULL)
		waveOutClose(mWaveOut);
	mWaveOut = NULL;
	mFFV = NULL;
	mFFA = NULL;
	mFrame = NULL;
	mVideoTrack = NULL;

	KillTimer(getHSelf(), TRACKBAR_TIMER);
	LeaveCriticalSection(&mDecoderSync);
}

void DockedPlayer::RenderVideoInternal() {
	int lastFrame = -1;
	char errmsg[1024] = { 0 };
	FFMS_ErrorInfo errinfo = { FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(errmsg), errmsg };
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(mVideoTrack);
	const FFMS_FrameInfo *finfo;
	HANDLE sleepEvent = CreateEvent(NULL, false, false, NULL);
	while (mState != PlaybackState::STATE_CLOSED) {
		EnterCriticalSection(&mDecoderSync);
		if (lastFrame != mVideoFrameIndex) {
			finfo = FFMS_GetFrameInfo(mVideoTrack, mVideoFrameIndex);
			mFrame = FFMS_GetFrame(mFFV, mVideoFrameIndex, &errinfo);
			lastFrame = mVideoFrameIndex;
			InvalidateRect(getHSelf(), &mVideoRect, false);
		}
		LeaveCriticalSection(&mDecoderSync);
		if (mState == PlaybackState::STATE_RUNNING) {
			int wait = (int)(finfo->PTS*base->Num/base->Den - mPlayStartPos - timeGetTime() + mPlayStartTime);
			if (wait < 0) {
				EnterCriticalSection(&mDecoderSync);
				while (timeGetTime() - mPlayStartTime > finfo->PTS*base->Num / (double)base->Den - mPlayStartPos && mVideoFrameIndex + 1 < mFFVP->NumFrames) {
					mVideoFrameIndex++;
					finfo = FFMS_GetFrameInfo(mVideoTrack, mVideoFrameIndex);
				}
				LeaveCriticalSection(&mDecoderSync);
			}else{
				timeSetEvent(wait, 10, (LPTIMECALLBACK)sleepEvent, NULL, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET);
				WaitForSingleObject(sleepEvent, 10);
			}
		} else {
			WaitForSingleObject(mDecodeVideoWait, INFINITE);
			if (mState == PlaybackState::STATE_CLOSED)
				break;
			ResetRendererSync();
		}
		if (mVideoFrameIndex == mFFVP->NumFrames - 1 || mVideoFrameIndex >= mVideoEndFrameIndex)
			Pause();
	}
	CloseHandle(sleepEvent);
	mRendererVideo = NULL;
}

DWORD WINAPI DockedPlayer::RenderVideoExternal(PVOID ptr) {
	((DockedPlayer*)ptr)->RenderVideoInternal();
	return 0;
}

void DockedPlayer::RenderAudioInternal() {
	char errmsg[1024] = { 0 };
	FFMS_ErrorInfo errinfo = { FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(errmsg), errmsg };
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(mVideoTrack);
	mPlayStartPos = 0;
	WAVEHDR _hdr = { 0 }, _hdr2 = { 0 };
	PWAVEHDR hdr = &_hdr, hdr2 = &_hdr2;
	char *buf = new char[1048576];
	char *buf2 = new char[1048576];
	hdr->lpData = buf;
	hdr2->lpData = buf2;
	while (mState != PlaybackState::STATE_CLOSED) {
		WaitForSingleObject(mDecodeAudioWait, INFINITE);
		while (mState == PlaybackState::STATE_RUNNING) {
			UINT32 dur = timeGetTime() - mPlayStartTime;
			if (dur < 100)
				dur = 100;
			int sampc = mFFAP->SampleRate * 100 / 1000;
			if (mAudioFrameIndex + sampc > mAudioEndFrameIndex)
				sampc = (int) (mAudioFrameIndex - mAudioEndFrameIndex);

			if (sampc <= 0)
				break;

			hdr->dwBufferLength = sampc * mFFAP->BitsPerSample * mFFAP->Channels / 8;
			FFMS_GetAudio(mFFA, hdr->lpData, mAudioFrameIndex, sampc, &errinfo);
			waveOutPrepareHeader(mWaveOut, hdr, sizeof(WAVEHDR));
			waveOutWrite(mWaveOut, hdr, sizeof(WAVEHDR));

			{
				PWAVEHDR _t = hdr; hdr = hdr2; hdr2 = _t;
			}

			mAudioFrameIndex += sampc;
			if ((mAudioFrameIndex - mAudioStartFrameIndex + 100) * 1000 / mFFAP->SampleRate < dur) // sync again if > 100ms
				mAudioFrameIndex = mFFAP->SampleRate * dur / 1000 + mAudioStartFrameIndex;

			if (mAudioFrameIndex >= mAudioEndFrameIndex)
				Pause();

			while (waveOutUnprepareHeader(mWaveOut, hdr, sizeof(WAVEHDR)) == WAVERR_STILLPLAYING && mState != PlaybackState::STATE_CLOSED)  {
				Sleep(10);
			}
		}
		waveOutReset(mWaveOut);
		waveOutUnprepareHeader(mWaveOut, hdr, sizeof(WAVEHDR));
		waveOutUnprepareHeader(mWaveOut, hdr2, sizeof(WAVEHDR));
	}
	mRendererAudio = NULL;
	delete[] buf;
	delete[] buf2;
}

void DockedPlayer::ResetRendererSync() {
	EnterCriticalSection(&mDecoderSync);
	const FFMS_FrameInfo *mFrameInfo = FFMS_GetFrameInfo(mVideoTrack, mVideoFrameIndex);
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(mVideoTrack);
	mPlayStartPos = mFrameInfo->PTS*base->Num / base->Den;
	mAudioFrameIndex = mAudioStartFrameIndex = (UINT64)(mFrameInfo->PTS * base->Num / (double)base->Den / 1000 * mFFAP->SampleRate);
	mPlayStartTime = timeGetTime();
	waveOutReset(mWaveOut);
	LeaveCriticalSection(&mDecoderSync);
}

DWORD WINAPI DockedPlayer::RenderAudioExternal(PVOID ptr) {
	((DockedPlayer*)ptr)->RenderAudioInternal();
	return 0;
}


HRESULT DockedPlayer::Play() {
	if (mState != STATE_PAUSED)
		return ERROR_INVALID_STATE;

	mState = STATE_RUNNING;
	ResetRendererSync();
	if (mFFVP != NULL) 
		mVideoEndFrameIndex = mFFVP->NumFrames;
	if(mFFAP != NULL)
		mAudioEndFrameIndex = mFFAP->NumSamples;
	SendMessage(mControls.mPlaybackSlider, TBM_SETPOS, true, (int)(GetTime()*mProgressMax / GetLength()));
	SetTimer(getHSelf(), TRACKBAR_TIMER, 250, NULL);
	SetEvent(mDecodeVideoWait);
	SetEvent(mDecodeAudioWait);
	return ERROR_SUCCESS;
}

HRESULT DockedPlayer::PlayRange(double start, double end) {
	if (mState != STATE_PAUSED)
		return ERROR_INVALID_STATE;

	SendMessage(mControls.mPlaybackSlider, TBM_SETPOS, true, (int)(start*mProgressMax / GetLength()));
	if (mFFVP != NULL) {
		mVideoFrameIndex = TimeToFrame(start);
		mVideoEndFrameIndex = TimeToFrame(end);
		if (mFFAP != NULL) {
			const FFMS_FrameInfo *mFrameInfo = FFMS_GetFrameInfo(mVideoTrack, mVideoEndFrameIndex);
			const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(mVideoTrack);
			mAudioEndFrameIndex = (UINT64)(mFrameInfo->PTS * base->Num / (double)base->Den / 1000 * mFFAP->SampleRate);
		}
	} else if (mFFAP != NULL) {
		mAudioEndFrameIndex = (UINT64)(end * mFFAP->SampleRate);
	}

	mState = STATE_RUNNING;
	ResetRendererSync();
	SetTimer(getHSelf(), TRACKBAR_TIMER, 250, NULL);
	SetEvent(mDecodeVideoWait);
	SetEvent(mDecodeAudioWait);
	return ERROR_SUCCESS;
}

HRESULT DockedPlayer::Pause() {
	if (mState != STATE_RUNNING)
		return ERROR_INVALID_STATE;
	KillTimer(getHSelf(), TRACKBAR_TIMER);
	waveOutReset(mWaveOut);
	mState = STATE_PAUSED;
	return ERROR_SUCCESS;
}

int DockedPlayer::TimeToFrame(double time) const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return -1;
	if(mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	if (time >= mFFAP->LastTime - mFFAP->FirstTime)
		return mFFVP->NumFrames - 1;
	else if (time < mFFAP->FirstTime)
		return 0;
	else {
		const FFMS_FrameInfo *info;
		int l = 0;
		int r = mFFVP->NumFrames;
		FFMS_Track *trk = FFMS_GetTrackFromVideo(mFFV);
		const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(trk);
		while (l + 1 < r) {
			int m = (l + r) / 2;
			info = FFMS_GetFrameInfo(trk, m);
			double t = (info->PTS * base->Num) / (double)base->Den / 1000;
			if (t < time)
				l = m;
			else
				r = m;
		}
		return l;
	}
}

double DockedPlayer::GetLength() const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	return mFFAP->LastTime - mFFAP->FirstTime;
}

HRESULT DockedPlayer::SetTime(double time) {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	SendMessage(mControls.mPlaybackSlider, TBM_SETPOS, true, (int)(time*mProgressMax / GetLength()));
	if(mFFV != NULL)
		mVideoFrameIndex = TimeToFrame(time);
	if (mState == STATE_RUNNING)
		ResetRendererSync();
	else
		SetEvent(mDecodeVideoWait);
	return ERROR_SUCCESS;
}

double DockedPlayer::GetTime() {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return -1;
	FFMS_Track *trk = FFMS_GetTrackFromVideo(mFFV);
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(trk);
	EnterCriticalSection(&mDecoderSync);
	const FFMS_FrameInfo *info = FFMS_GetFrameInfo(trk, mVideoFrameIndex);
	double res = ((info->PTS * base->Num) / (double)base->Den) / 1000;
	LeaveCriticalSection(&mDecoderSync);
	return res;
}

ULONGLONG DockedPlayer::GetFrames() const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	if (mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	return mFFVP->NumFrames;
}
ULONGLONG DockedPlayer::GetFrame() const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	if (mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	return mVideoFrameIndex;
}
HRESULT DockedPlayer::SetFrame(ULONGLONG frame) {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return -1;
	if (mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	mVideoFrameIndex = (int) max(0, min(frame, mFFVP->NumFrames - 1));
	if (mState == STATE_RUNNING) {
		ResetRendererSync();
	} else {
		SetEvent(mDecodeVideoWait);
	}
	return ERROR_SUCCESS;
}

BOOL DockedPlayer::HasVideo() const {
	return mFFV != NULL;
}

BOOL CALLBACK DockedPlayer::run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG: {
			mControls.mPlaybackSlider = GetDlgItem(getHSelf(), IDC_PLAYER_NAVIGATE);
			mControls.mOpenProgress = GetDlgItem(getHSelf(), IDC_OPEN_PROGRES);
			mControls.mOpenCancel = GetDlgItem(getHSelf(), IDC_CANCEL_OPENING);
			SendMessage(mControls.mPlaybackSlider, TBM_SETRANGEMIN, TRUE, 0);
			SendMessage(mControls.mPlaybackSlider, TBM_SETRANGEMAX, TRUE, mProgressMax);
			SendMessage(mControls.mOpenProgress, PBM_SETRANGE32, 0, mProgressMax);
			ShowWindow(mControls.mOpenProgress, SW_HIDE);
			ShowWindow(mControls.mOpenCancel, SW_HIDE);
			break;
		}
		case WM_COMMAND: {
			if (wParam == IDC_CANCEL_OPENING) {
				mFFLoadCancel = true;
			}
			break;
		}
		case WM_HSCROLL: {
			SetTime(GetLength() * SendMessage(mControls.mPlaybackSlider, TBM_GETPOS, NULL, NULL) / mProgressMax);
			break;
		}
		case WM_TIMER: {
			if (wParam == TRACKBAR_TIMER) {
				SendMessage(mControls.mPlaybackSlider, TBM_SETPOS, true, (int)(GetTime()*mProgressMax / GetLength()));
			}
			break;
		}
		case WM_SIZE: {
			RECT r, r2;
			this->getClientRect(r);
			if (mFrame != NULL) {
				if (mFrame->ScaledHeight != 0) {
					r.right -= r.left; r.left = 0;
					r.top = 0;
					r.bottom = r.right * mFrame->ScaledHeight / mFrame->ScaledWidth;
					m_clientTop = r.bottom;
					mVideoRect = r;
				}
			} else
				m_clientTop = 0;
			GetWindowRect(mControls.mPlaybackSlider, &r2);
			SetWindowPos(mControls.mPlaybackSlider, NULL, 0, m_clientTop, r.right, r2.bottom - r2.top, SWP_NOACTIVATE);
			GetWindowRect(mControls.mOpenCancel, &r2);
			SetWindowPos(mControls.mOpenProgress, NULL, 0, 0, r.right - (r2.right - r2.left), r2.bottom - r2.top, SWP_NOACTIVATE);
			SetWindowPos(mControls.mOpenCancel, NULL, r.right - (r2.right - r2.left), 0, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
			InvalidateRect(getHSelf(), NULL, false);
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
			bool nf = GetFocus() == getHSelf() || GetFocus() == mControls.mPlaybackSlider;
			if (m_focused == nf)
				break;
			m_focused = nf;
			RECT rc;
			GetClientRect(mControls.mPlaybackSlider, &rc);
			rc.left = 10;
			rc.right -= 10;
			rc.top = m_clientTop + rc.bottom + 10;
			rc.bottom = rc.top + 600;
			InvalidateRect(getHSelf(), &rc, false);
			break;
		}
		case WM_PAINT: {
			PAINTSTRUCT ps;
			RECT self, rc;
			HDC hdc;
			GetClientRect(getHSelf(), &self);

			hdc = BeginPaint(getHSelf(), &ps);

			if (State() != STATE_CLOSED && HasVideo()) {
				if (mFrame != NULL) {
					SetStretchBltMode(hdc, HALFTONE);
					SetBrushOrgEx(hdc, 0, 0, NULL);
					StretchDIBits(hdc, 0, m_clientTop, self.right, -m_clientTop, 0, 0, mVideoBitmapInfo.bmiHeader.biWidth, mVideoBitmapInfo.bmiHeader.biHeight, mFrame->Data[0], &mVideoBitmapInfo, DIB_RGB_COLORS, SRCCOPY);
					double t = GetTime();
					if (t < 0) t = 0;
					TCHAR msg[1024];
					wsprintf(msg, L"%06d %02d:%02d:%02d.%03d", mVideoFrameIndex, (int)(t / 3600), (int)(t / 60) % 60, (int)(t) % 60, (int)(t * 1000) % 1000);
					TextOut(hdc, 10, 10, msg, wcslen(msg));
				}
			}
			self.top = m_clientTop;
			FillRect(hdc, &self, (HBRUSH)(COLOR_WINDOW + 1));
			if (m_focused) {
				TCHAR *msg = L""
					"Left: -3 sec\n"
					"Right: +3 sec\n"
					"Ctrl+Left: -1 frame\n"
					"Ctrl+Right: +1 frame\n"
					"Space: Play / Pause\n"
					"Z, Num0: Undo\n"
					"X, Up: Insert timecode\n"
					"C, Down: Insert stop timecode";
				GetClientRect(mControls.mPlaybackSlider, &rc);
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
			CloseFile();
			break;
		}
		case WM_GRAPH_EVENT: {
			// TODO ??
			break;
		}
	}
	return DockingDlgInterface::run_dlgProc(message, wParam, lParam);
}

