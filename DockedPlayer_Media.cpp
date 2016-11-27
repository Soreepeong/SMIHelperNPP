#include "DockedPlayer.h"
#include "PluginInterface.h"
#include <Commctrl.h>
#include <Mmsystem.h>

DockedPlayer::Media::Media(TCHAR *pszFileName, DockedPlayer *player) :
	mDockedPlayer(player){
	mDecodeAudioWait = CreateEvent(NULL, false, false, NULL);
	mDecodeVideoWait = CreateEvent(NULL, false, false, NULL);
	InitializeCriticalSection(&mDecoderSync);
	mVideoBitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	mVideoBitmapInfo.bmiHeader.biBitCount = 32;
	mVideoBitmapInfo.bmiHeader.biPlanes = 1;
	mVideoBitmapInfo.bmiHeader.biCompression = BI_RGB;
	mVideoBitmapInfo.bmiHeader.biClrUsed = 0;
	struct _data {
		DockedPlayer::Media *t;
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
	mFFLoader = CreateThread(NULL, NULL, DockedPlayer::Media::OpenFileExternal, (PVOID)data, NULL, NULL);
}

DockedPlayer::Media::~Media() {
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

	mDockedPlayer->StopTrackbarUpdate(this);
	CloseHandle(mDecodeAudioWait);
	CloseHandle(mDecodeVideoWait);
	LeaveCriticalSection(&mDecoderSync);
	DeleteCriticalSection(&mDecoderSync);
}

void DockedPlayer::Media::OpenFileInternal(TCHAR *u16, char *u8, TCHAR *iu16, char *iu8) {
	FFMS_Index			*index = NULL;
	char				errmsg[1024] = { 0 };
	FFMS_ErrorInfo		errinfo = { FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(errmsg), errmsg };
	DWORD flags = 0;
	struct {
		DockedPlayer::Media *player;
		UINT32 lastCall;
	} loaderCallbackData = { this, 0 };

	mState = PlaybackState::STATE_OPENING;
	mDockedPlayer->OnProgressCallback(this, true, 0);

	if (PathFileExists(iu16))
		index = FFMS_ReadIndex(iu8, &errinfo);
	if (index == NULL) {
		mFFLoadCancel = false;
		if ((mFFIndexer = FFMS_CreateIndexer(u8, &errinfo)) == NULL) goto done;
		FFMS_SetProgressCallback(mFFIndexer, DockedPlayer::Media::FFOpenCallbackExternal, &loaderCallbackData);
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

		if (waveOutOpen(&mWaveOut, WAVE_MAPPER, &mWaveFormat, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) goto done;
	}

done:
	if (index != NULL)
		FFMS_DestroyIndex(index);

	CloseHandle(mFFLoader);
	mFFLoader = INVALID_HANDLE_VALUE;
	if (errinfo.ErrorType != FFMS_ERROR_SUCCESS) {
		strncpy(mErrorMsg, errinfo.Buffer, sizeof(mErrorMsg));
		mState = PlaybackState::STATE_ERROR;
	} else {
		mState = PlaybackState::STATE_PAUSED;
		mDockedPlayer->OnSizeRequested(this);
		mRendererAudio = CreateThread(NULL, NULL, DockedPlayer::Media::RenderAudioExternal, (PVOID)this, NULL, NULL);
		mRendererVideo = CreateThread(NULL, NULL, DockedPlayer::Media::RenderVideoExternal, (PVOID)this, NULL, NULL);
	}
	mDockedPlayer->OnProgressCallback(this, false, 0);
}
DWORD WINAPI DockedPlayer::Media::OpenFileExternal(PVOID ptr) {
	struct _data {
		DockedPlayer::Media *t;
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
void DockedPlayer::Media::CancelOpening() {
	mFFLoadCancel = true;
}

int FFMS_CC DockedPlayer::Media::FFOpenCallbackInternal(int64_t Current, int64_t Total, UINT32 &lastCall) {
	if (lastCall + 50 < GetTickCount()) {
		mDockedPlayer->OnProgressCallback(this, true, static_cast<int>((double)DockedPlayer::mProgressMax / Total * Current));
		lastCall = GetTickCount();
	}
	return mFFLoadCancel;
}
int FFMS_CC DockedPlayer::Media::FFOpenCallbackExternal(int64_t Current, int64_t Total, void *ICPrivate) {
	struct _data {
		DockedPlayer::Media *player;
		UINT32 lastCall;
	} *data = reinterpret_cast<_data*>(ICPrivate);
	return data->player->FFOpenCallbackInternal(Current, Total, data->lastCall);
}

void DockedPlayer::Media::RenderVideoInternal() {
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
			mDockedPlayer->UpdateFrame(this);
		}
		LeaveCriticalSection(&mDecoderSync);
		if (mState == PlaybackState::STATE_RUNNING) {
			int wait = (int)(finfo->PTS*base->Num / base->Den - mPlayStartPos - timeGetTime() + mPlayStartTime);
			if (wait < 0) {
				EnterCriticalSection(&mDecoderSync);
				while (timeGetTime() - mPlayStartTime > finfo->PTS*base->Num / (double)base->Den - mPlayStartPos && mVideoFrameIndex + 1 < mFFVP->NumFrames) {
					mVideoFrameIndex++;
					finfo = FFMS_GetFrameInfo(mVideoTrack, mVideoFrameIndex);
				}
				LeaveCriticalSection(&mDecoderSync);
			} else {
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

DWORD WINAPI DockedPlayer::Media::RenderVideoExternal(PVOID ptr) {
	((DockedPlayer::Media*)ptr)->RenderVideoInternal();
	return 0;
}

void DockedPlayer::Media::RenderAudioInternal() {
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
				sampc = (int)(mAudioFrameIndex - mAudioEndFrameIndex);

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

			while (waveOutUnprepareHeader(mWaveOut, hdr, sizeof(WAVEHDR)) == WAVERR_STILLPLAYING && mState != PlaybackState::STATE_CLOSED) {
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

void DockedPlayer::Media::ResetRendererSync() {
	EnterCriticalSection(&mDecoderSync);
	const FFMS_FrameInfo *mFrameInfo = FFMS_GetFrameInfo(mVideoTrack, mVideoFrameIndex);
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(mVideoTrack);
	mPlayStartPos = mFrameInfo->PTS*base->Num / base->Den;
	mAudioFrameIndex = mAudioStartFrameIndex = (UINT64)(mFrameInfo->PTS * base->Num / (double)base->Den / 1000 * mFFAP->SampleRate);
	mPlayStartTime = timeGetTime();
	waveOutReset(mWaveOut);
	LeaveCriticalSection(&mDecoderSync);
}

DWORD WINAPI DockedPlayer::Media::RenderAudioExternal(PVOID ptr) {
	((DockedPlayer::Media*)ptr)->RenderAudioInternal();
	return 0;
}


HRESULT DockedPlayer::Media::Play() {
	if (mState != STATE_PAUSED)
		return ERROR_INVALID_STATE;

	mState = STATE_RUNNING;
	ResetRendererSync();
	if (mFFVP != NULL)
		mVideoEndFrameIndex = mFFVP->NumFrames;
	if (mFFAP != NULL)
		mAudioEndFrameIndex = mFFAP->NumSamples;
	mDockedPlayer->SetTrackbarPosition(this, (int)(GetTime()*mProgressMax / GetLength()));
	mDockedPlayer->StartTrackbarUpdate(this);
	SetEvent(mDecodeVideoWait);
	SetEvent(mDecodeAudioWait);
	return ERROR_SUCCESS;
}

HRESULT DockedPlayer::Media::PlayRange(double start, double end) {
	if (mState != STATE_PAUSED)
		return ERROR_INVALID_STATE;

	mDockedPlayer->SetTrackbarPosition(this, (int)(start*mProgressMax / GetLength()));
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
	mDockedPlayer->StartTrackbarUpdate(this);
	SetEvent(mDecodeVideoWait);
	SetEvent(mDecodeAudioWait);
	return ERROR_SUCCESS;
}

HRESULT DockedPlayer::Media::Pause() {
	if (mState != STATE_RUNNING)
		return ERROR_INVALID_STATE;
	mDockedPlayer->StopTrackbarUpdate(this);
	waveOutReset(mWaveOut);
	mState = STATE_PAUSED;
	return ERROR_SUCCESS;
}

int DockedPlayer::Media::TimeToFrame(double time) const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return -1;
	if (mFFVP == NULL)
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

double DockedPlayer::Media::GetLength() const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	return mFFAP->LastTime - mFFAP->FirstTime;
}

HRESULT DockedPlayer::Media::SetTime(double time, bool updateTrackbarPosition) {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	if(updateTrackbarPosition)
		mDockedPlayer->SetTrackbarPosition(this, (int)(time*mProgressMax / GetLength()));
	if (mFFV != NULL)
		mVideoFrameIndex = TimeToFrame(time);
	if (mState == STATE_RUNNING)
		ResetRendererSync();
	else
		SetEvent(mDecodeVideoWait);
	return ERROR_SUCCESS;
}

double DockedPlayer::Media::GetTime() {
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

ULONGLONG DockedPlayer::Media::GetFrameCount() const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	if (mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	return mFFVP->NumFrames;
}
ULONGLONG DockedPlayer::Media::GetFrameIndex() const {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return ERROR_INVALID_STATE;
	if (mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	return mVideoFrameIndex;
}
HRESULT DockedPlayer::Media::SetFrameIndex(ULONGLONG frame, bool updateTrackbarPosition) {
	if (mState == STATE_CLOSED || mState == STATE_OPENING)
		return -1;
	if (mFFVP == NULL)
		return ERROR_INVALID_OPERATION;
	mVideoFrameIndex = (int)max(0, min(frame, mFFVP->NumFrames - 1));
	if (updateTrackbarPosition)
		mDockedPlayer->SetTrackbarPosition(this, (int)((double)mVideoFrameIndex / mFFVP->NumFrames*mProgressMax / GetLength()));
	if (mState == STATE_RUNNING) {
		ResetRendererSync();
	} else {
		SetEvent(mDecodeVideoWait);
	}
	return ERROR_SUCCESS;
}

BOOL DockedPlayer::Media::HasVideo() const {
	return mFFV != NULL;
}