#include "DockedPlayer.h"
#include "PluginInterface.h"
#include <Commctrl.h>
#define TRACKBAR_TIMER 10000

extern NppData nppData;
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto done; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

DockedPlayer::DockedPlayer() :
	DockingDlgInterface(IDD_PLAYER){
	FFMS_Init(0, 0);
	mDecodeWait = CreateEvent(NULL, false, false, NULL);
	InitializeCriticalSection(&mDecoderSync);
	mVideoBitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	mVideoBitmapInfo.bmiHeader.biBitCount = 32;
	mVideoBitmapInfo.bmiHeader.biPlanes = 1;
	mVideoBitmapInfo.bmiHeader.biCompression = BI_RGB;
	mVideoBitmapInfo.bmiHeader.biClrUsed = 0;
}


DockedPlayer::~DockedPlayer() {
	CloseFile();
	CloseHandle(mDecodeWait);
	DeleteCriticalSection(&mDecoderSync);
}

void DockedPlayer::OpenFileInternal(TCHAR *u16, char *u8, TCHAR *iu16, char *iu8) {
	FFMS_Index			*index = NULL;
	char				errmsg[1024] = { 0 };
	FFMS_ErrorInfo		errinfo = { FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(errmsg), errmsg };
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	IMMDeviceEnumerator *pEnumerator = NULL;
	DWORD flags = 0;
	CoInitialize(NULL);
	hr = CoCreateInstance( CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
	EXIT_ON_ERROR(hr);
	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	EXIT_ON_ERROR(hr);
	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr);

	mState = PlaybackState::STATE_OPENING;
	if (PathFileExists(iu16))
		index = FFMS_ReadIndex(iu8, &errinfo);
	if (index == NULL) {
		if ((mFFIndexer = FFMS_CreateIndexer(u8, &errinfo)) == NULL) goto done;
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
		mFrameIndex = 0;
		if ((mFFV = FFMS_CreateVideoSource(u8, vidId, index, 1, FFMS_SEEK_NORMAL, &errinfo)) == NULL) goto done;
		mFFVP = FFMS_GetVideoProperties(mFFV);
		if ((mFrame = FFMS_GetFrame(mFFV, mFrameIndex, &errinfo)) == NULL) goto done;
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
	}
	pwfx.nBlockAlign = mFFAP->BitsPerSample * mFFAP->Channels / 8;
	pwfx.nAvgBytesPerSec = pwfx.nBlockAlign * mFFAP->SampleRate;
	pwfx.nChannels = mFFAP->Channels;
	pwfx.nSamplesPerSec = mFFAP->SampleRate;
	pwfx.wBitsPerSample = mFFAP->BitsPerSample;
	pwfx.wFormatTag = mFFAP->SampleFormat == FFMS_FMT_FLT || mFFAP->SampleFormat == FFMS_FMT_DBL ?
		WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
	PWAVEFORMATEX wfx;
	pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &pwfx, &wfx);
	// memcpy((PVOID)&pwfx, wfx, sizeof(pwfx));
	CoTaskMemFree(wfx);
	hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, &pwfx, NULL);
	EXIT_ON_ERROR(hr);
	hr = pAudioClient->GetService( IID_IAudioRenderClient, (void**)&pRenderClient);
	EXIT_ON_ERROR(hr);

done:
	if(index != NULL)
		FFMS_DestroyIndex(index);

	CloseHandle(mFFLoader);
	mFFLoader = INVALID_HANDLE_VALUE;
	if (errinfo.ErrorType != FFMS_ERROR_SUCCESS || FAILED(hr)) {
		CloseFile();
	} else {
		mState = PlaybackState::STATE_PAUSED;
		PostMessage(getHSelf(), WM_SIZE, NULL, NULL);
		mRenderer = CreateThread(NULL, NULL, DockedPlayer::RendererExternal, (PVOID)this, NULL, NULL);
	}
	SAFE_RELEASE(pEnumerator);
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
	delete data->utf16;
	delete data->utf8;
	delete data->index_utf16;
	delete data->index_utf8;
	delete data;
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

void DockedPlayer::CloseFile() {
	EnterCriticalSection(&mDecoderSync);
	if (mState == STATE_RUNNING)
		pAudioClient->Stop();
	mState = STATE_CLOSED;
	if (mFFLoader != INVALID_HANDLE_VALUE) {
		FFMS_CancelIndexing(mFFIndexer);
		if (WaitForSingleObject(mFFLoader, 1000) == WAIT_TIMEOUT)
			TerminateThread(mFFLoader, -1);
		CloseHandle(mFFLoader);
		mFFIndexer = NULL;
		mFFLoader = INVALID_HANDLE_VALUE;
	}
	mPlayResetSync = true;
	if (mRenderer != INVALID_HANDLE_VALUE) {
		SetEvent(mDecodeWait);
		if (WaitForSingleObject(mRenderer, 1000) == WAIT_TIMEOUT)
			TerminateThread(mRenderer, -1);
		CloseHandle(mRenderer);
		mRenderer = INVALID_HANDLE_VALUE;
	}
	if (mFFV != NULL)
		FFMS_DestroyVideoSource(mFFV);
	if (mFFA != NULL)
		FFMS_DestroyAudioSource(mFFA);
	mFFV = NULL;
	mFFA = NULL;
	mFrame = NULL;
	mVideoTrack = NULL;
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pRenderClient);

	KillTimer(getHSelf(), TRACKBAR_TIMER);
	LeaveCriticalSection(&mDecoderSync);
}

void DockedPlayer::RendererInternal() {
	int lastFrame = -1, lastAudio = -1;
	double mPlayStart = 0;
	char errmsg[1024] = { 0 };
	int sampc = 0, dur = 0;
	FFMS_ErrorInfo		errinfo = { FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof(errmsg), errmsg };
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(mVideoTrack);
	LARGE_INTEGER StartingTime, EndingTime;
	LARGE_INTEGER Frequency;
	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);
	mPlayStart = 0;
	while (mState != PlaybackState::STATE_CLOSED) {
		EnterCriticalSection(&mDecoderSync);
		if (mPlayResetSync) {
			mFrameInfo = FFMS_GetFrameInfo(mVideoTrack, mFrameIndex);
			mPlayStart = mFrameInfo->PTS*base->Num / (double)base->Den;
			mPlayResetSync = false;
			QueryPerformanceCounter(&StartingTime);
		}
		if (lastFrame != mFrameIndex) {
			mFrame = FFMS_GetFrame(mFFV, mFrameIndex, &errinfo);
			mFrameInfo = FFMS_GetFrameInfo(mVideoTrack, mFrameIndex + 1);
			if (mFrameInfo == NULL) {
				sampc = dur = 0;
			} else {
				sampc = mFrameInfo->PTS;
				mFrameInfo = FFMS_GetFrameInfo(mVideoTrack, mFrameIndex);
				dur = (int)((sampc - mFrameInfo->PTS) * base->Num / (double)base->Den);
				sampc = (int)((sampc - mFrameInfo->PTS) * base->Num / (double)base->Den / 1000 * mFFAP->SampleRate);
			}
			lastFrame = mFrameIndex;
			InvalidateRect(getHSelf(), &mVideoRect, false);
			if (mState == PlaybackState::STATE_RUNNING && sampc != 0) {
				BYTE *pData;
				UINT32  pad;
				pAudioClient->GetCurrentPadding(&pad);
				if (pad == 0) {
					pRenderClient->GetBuffer(sampc, &pData);
					FFMS_GetAudio(mFFA, pData, mFFAP->NumSamples * mFrameIndex / mFFVP->NumFrames, sampc, &errinfo);
					pRenderClient->ReleaseBuffer(sampc, NULL);
				}
				//*/
			}
		}
		LeaveCriticalSection(&mDecoderSync);
		if (mState == PlaybackState::STATE_RUNNING) {
			while (mState != PlaybackState::STATE_CLOSED) {
				QueryPerformanceCounter(&EndingTime);
				int dec = (int)((EndingTime.QuadPart - StartingTime.QuadPart) * 1000 / Frequency.QuadPart);
				if (dec >= dur)
					break;
				Sleep(10);
			}
			
			QueryPerformanceCounter(&EndingTime);
			int dec = (int)((EndingTime.QuadPart - StartingTime.QuadPart) * 1000 / Frequency.QuadPart);
			EnterCriticalSection(&mDecoderSync);
			if(dec > mFrameInfo->PTS*base->Num / (double)base->Den - mPlayStart && mFrameIndex + 1 < mFFVP->NumFrames && !mPlayResetSync)
				mFrameIndex++;
			LeaveCriticalSection(&mDecoderSync);
		} else {
			WaitForSingleObject(mDecodeWait, INFINITE);
			QueryPerformanceCounter(&StartingTime);
		}
		if (mFrameIndex == mFFVP->NumFrames - 1)
			Pause();
	}
	mRenderer = NULL;
}

DWORD WINAPI DockedPlayer::RendererExternal(PVOID ptr) {
	((DockedPlayer*)ptr)->RendererInternal();
	return 0;
}


HRESULT DockedPlayer::Play() {
	if (mState != STATE_PAUSED)
		return ERROR_INVALID_STATE;

	mState = STATE_RUNNING;
	mPlayResetSync = true;
	SendMessage(m_slider, TBM_SETPOS, true, (int)(GetTime()*100000000. / GetLength()));
	SetTimer(getHSelf(), TRACKBAR_TIMER, 250, NULL);
	SetEvent(mDecodeWait);
	pAudioClient->Start();
	return ERROR_SUCCESS;
}

HRESULT DockedPlayer::Pause() {
	if (mState != STATE_RUNNING)
		return ERROR_INVALID_STATE;

	KillTimer(getHSelf(), TRACKBAR_TIMER);
	pAudioClient->Stop();
	mState = STATE_PAUSED;
	return ERROR_SUCCESS;
}

double DockedPlayer::GetLength() const {
	if (mState == STATE_CLOSED)
		return ERROR_INVALID_STATE;
	return mFFAP->LastTime - mFFAP->FirstTime;
}

HRESULT DockedPlayer::SetTime(double time) {
	if (mState == STATE_CLOSED)
		return ERROR_INVALID_STATE;
	EnterCriticalSection(&mDecoderSync);
	SendMessage(m_slider, TBM_SETPOS, true, (int)(time*100000000. / GetLength()));
	if(mFFV != NULL){
		if (time >= mFFAP->LastTime - mFFAP->FirstTime)
			mFrameIndex = mFFVP->NumFrames - 1;
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
			mFrameIndex = l;
		}
	}
	if (mState == STATE_RUNNING) {
		mPlayResetSync = true;
	}else
		SetEvent(mDecodeWait);
	LeaveCriticalSection(&mDecoderSync);
	return ERROR_SUCCESS;
}

double DockedPlayer::GetTime() {
	if (mState == STATE_CLOSED)
		return -1;
	FFMS_Track *trk = FFMS_GetTrackFromVideo(mFFV);
	const FFMS_TrackTimeBase *base = FFMS_GetTimeBase(trk);
	EnterCriticalSection(&mDecoderSync);
	const FFMS_FrameInfo *info = FFMS_GetFrameInfo(trk, mFrameIndex);
	double res = ((info->PTS * base->Num) / (double)base->Den) / 1000;
	LeaveCriticalSection(&mDecoderSync);
	return res;
}

BOOL DockedPlayer::HasVideo() const {
	return mFFV != NULL;
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
			SetTime(GetLength() * k);
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
			GetWindowRect(m_slider, &r2);
			SetWindowPos(m_slider, NULL, 0, m_clientTop, r.right, r2.bottom - r2.top, SWP_NOACTIVATE);
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
			RECT self;
			HDC hdc;
			GetClientRect(getHSelf(), &self);

			hdc = BeginPaint(getHSelf(), &ps);

			if (State() != STATE_CLOSED && HasVideo()) {
				// The player has video, so ask the player to repaint. 
				if (mFrame != NULL) {
					SetStretchBltMode(hdc, HALFTONE);
					SetBrushOrgEx(hdc, 0, 0, NULL);
					StretchDIBits(hdc, 0, m_clientTop, self.right, -m_clientTop, 0, 0, mVideoBitmapInfo.bmiHeader.biWidth, mVideoBitmapInfo.bmiHeader.biHeight, mFrame->Data[0], &mVideoBitmapInfo, DIB_RGB_COLORS, SRCCOPY);
				}
			}
			self.top = m_clientTop;
			FillRect(hdc, &self, (HBRUSH)(COLOR_WINDOW + 1));
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

