#include "DockedPlayer.h"
#include "PluginInterface.h"
#include "PluginDefinition.h"
#include <Commctrl.h>
#include <Mmsystem.h>
#include <Windowsx.h>
#ifdef min
#undef min
#undef max
#endif
#define TRACKBAR_TIMER 10000

extern NppData nppData;

DockedPlayer::DockedPlayer() :
	DockingDlgInterface(IDD_PLAYER){
	FFMS_Init(0, 0);
}

DockedPlayer::~DockedPlayer() {
	for (std::map<std::wstring, Media*>::iterator i = mMedia.begin(); i != mMedia.end(); ++i)
		delete i->second;
	mMedia.clear();
}

std::vector<std::wstring> DockedPlayer::GetOpenIds() const {
	std::vector<std::wstring> res;
	for (std::map<std::wstring, Media*>::const_iterator i = mMedia.cbegin(); i != mMedia.cend(); ++i)
		res.push_back(i->first);
	return res;
}

void DockedPlayer::SetTab(std::wstring id) {
	if (id == mCurrentTab)
		return;
	if (mMedia.count(mCurrentTab) > 0)
		mMedia[mCurrentTab]->Pause();
	mCurrentTab = id;
	SendMessage(getHSelf(), WM_SIZE, 0, 0);
	Invalidate();
	if (mMedia.count(mCurrentTab) > 0)
		PostMessage(mControls.mPlaybackSlider, TBM_SETPOS, TRUE,
			(int)((double)mProgressMax * mMedia[mCurrentTab]->GetFrameIndex() / mMedia[mCurrentTab]->GetFrameCount()));
	else
		PostMessage(mControls.mPlaybackSlider, TBM_SETPOS, TRUE, 0);
}

void DockedPlayer::CloseTab(std::wstring id) {
	if (mMedia.count(id) > 0)
		mMedia.erase(id);
}

DockedPlayer::Media* DockedPlayer::GetMedia() {
	if (mMedia.count(mCurrentTab) > 0)
		return mMedia[mCurrentTab];
	return NULL;
}

void DockedPlayer::OpenFile(TCHAR *pszFileName) {
	if (mMedia.count(mCurrentTab) > 0) {
		if (mMedia[mCurrentTab]->State() == PlaybackState::STATE_OPENING)
			return;
		delete mMedia[mCurrentTab];
	}
	mMedia[mCurrentTab] = new Media(pszFileName, this);
}

void DockedPlayer::OnProgressCallback(DockedPlayer::Media *media, bool visible, int progress) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	mControls.showOpeningWindows(visible);
	PostMessage(mControls.mOpenProgress, PBM_SETPOS, progress, 0);
}

void DockedPlayer::OnSizeRequested(DockedPlayer::Media *media) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	SendMessage(getHSelf(), WM_SIZE, 0, 0);
}

void DockedPlayer::UpdateFrame(DockedPlayer::Media *media) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	Invalidate();
}

void DockedPlayer::SetTrackbarPosition(DockedPlayer::Media *media, int position) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	PostMessage(mControls.mPlaybackSlider, TBM_SETPOS, TRUE, position);
}

void DockedPlayer::StartTrackbarUpdate(DockedPlayer::Media *media) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	SetTimer(getHSelf(), TRACKBAR_TIMER, 50, NULL);
}

void DockedPlayer::StopTrackbarUpdate(DockedPlayer::Media *media) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	KillTimer(getHSelf(), TRACKBAR_TIMER);
}

void DockedPlayer::Invalidate(RECT *r, bool erase) {
	InvalidateRect(getHSelf(), r, erase);
}

void DockedPlayer::ScrollWaveSlider(int to) {
	if (mMedia.count(mCurrentTab) == 0)
		return;
	Media *media = mMedia[mCurrentTab];
	WaveView *wv = media->GetWaveView();
	if (wv == NULL)
		return;
	bool playPos = to == -1;
	if (playPos) {
		mWaveCurrentPosLeftScreen = false;
		to = media->GetAudioFrameIndex();
	}
	int x = wv->GetX(to);
	if (x < 0)
		wv->ScrollTo(to);
	if(!playPos)
		mWaveCurrentPosLeftScreen = media->GetWaveView()->GetX(media->GetAudioFrameIndex()) < 0;
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
				if (mMedia.count(mCurrentTab) > 0)
					mMedia[mCurrentTab]->CancelOpening();
			}
			break;
		}
		case WM_HSCROLL: {
			if (mMedia.count(mCurrentTab) > 0)
				mMedia[mCurrentTab]->SetFrameIndex(mMedia[mCurrentTab]->GetFrameCount() *SendMessage(mControls.mPlaybackSlider, TBM_GETPOS, NULL, NULL) / mProgressMax, false);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP: {
			int mx = GET_X_LPARAM(lParam);
			int my = GET_Y_LPARAM(lParam);
			if (RectCheck(mWaveRect, mx, my) ||
				(mMouseAt == WAVEFORM && message == WM_LBUTTONUP)) {
				mMouseAt = message == WM_LBUTTONDOWN ? WAVEFORM : NONE;
				createUndoPoint(message == WM_LBUTTONDOWN);
				Media *media = mMedia[mCurrentTab];
				setSubtitleCode(std::max(0., std::min(media->GetLength(), media->GetWaveView()->GetSampleIndex(mx) / (double)media->GetAudioProperties()->SampleRate)));
				Invalidate();
			} else if (RectCheck(mWaveSliderRect, mx, my) ||
				(mMouseAt == WAVEFORM_SLIDER && message == WM_LBUTTONUP)) {
				mMouseAt = message == WM_LBUTTONDOWN ? WAVEFORM_SLIDER : NONE;
				Media *media = mMedia[mCurrentTab];
				int samples = media->GetAudioProperties()->NumSamples;
				samples -= (int)(samples / media->GetWaveView()->GetZoom());
				if (message == WM_LBUTTONDOWN) {
					int sliderLeft = samples == 0 ? 0 : (int)((double)(mWaveSliderRect.right - mWaveSliderRect.left - mWaveSliderWidth) * media->GetWaveView()->GetScroll() / samples);
					if (mx < sliderLeft || mx > sliderLeft + mWaveSliderWidth)
						mWaveSliderScrollBase = mWaveSliderWidth / 2;
					else
						mWaveSliderScrollBase = sliderLeft + mWaveSliderWidth - mx;
				}
				double pos = (double)(mx + mWaveSliderScrollBase - mWaveSliderRect.left - mWaveSliderWidth) / (mWaveSliderRect.right - mWaveSliderRect.left - mWaveSliderWidth);
				pos = std::max(0., std::min(1., pos));
				media->GetWaveView()->ScrollTo((int)(samples * pos));
				mWaveCurrentPosLeftScreen = media->GetWaveView()->GetX(media->GetAudioFrameIndex()) < 0;
				Invalidate(&mWaveRect);
				Invalidate(&mWaveSliderRect);
			} else if (RectCheck(mVideoSizeLimiter, mx, my) ||
				(mMouseAt == LIMITER_VIDEO && message == WM_LBUTTONUP)) {
				mMouseAt = message == WM_LBUTTONDOWN ? LIMITER_VIDEO : NONE;
				mVideoMaxHeight = std::max(32, my);
				SendMessage(getHSelf(), WM_SIZE, 0, 0);
				Invalidate();
			} else if (RectCheck(mWaveSizeLimiter, mx, my) ||
				(mMouseAt == LIMITER_WAVEFORM && message == WM_LBUTTONUP)) {
				mMouseAt = message == WM_LBUTTONDOWN ? LIMITER_WAVEFORM : NONE;
				mWaveMaxHeight = std::max(32, my - (int) mWaveRect.top);
				Invalidate();
			} else
				mMouseAt = NONE;
			if (message == WM_LBUTTONDOWN)
				SetCapture(getHSelf());
			else
				ReleaseCapture();
			SetFocus(getHSelf());
			break;
		}
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP: {
			int mx = GET_X_LPARAM(lParam);
			int my = GET_Y_LPARAM(lParam);
			if (RectCheck(mWaveRect, mx, my) ||
				(mMouseAt == WAVEFORM && message == WM_RBUTTONUP)) {
				createUndoPoint(message == WM_RBUTTONDOWN);
				mMouseAt = message == WM_RBUTTONDOWN ? WAVEFORM : NONE;
				Media *media = mMedia[mCurrentTab];
				setSubtitleCodeEmpty(std::max(0., std::min(media->GetLength(), media->GetWaveView()->GetSampleIndex(mx) / (double)media->GetAudioProperties()->SampleRate)));
				Invalidate();
			}
			if (message == WM_RBUTTONDOWN)
				SetCapture(getHSelf());
			else
				ReleaseCapture();
			SetFocus(getHSelf());
			break;
		}
		case WM_MOUSEMOVE:
			if (wParam & (MK_LBUTTON | MK_RBUTTON)) {
				int mx = GET_X_LPARAM(lParam);
				int my = GET_Y_LPARAM(lParam);
				if (mMouseAt == WAVEFORM) {
					Media *media = mMedia[mCurrentTab];
					double time = media->GetWaveView()->GetSampleIndex(mx) / (double)media->GetAudioProperties()->SampleRate;
					time = std::max(0., std::min(media->GetLength(), time));
					if (wParam & MK_LBUTTON)
						setSubtitleCode(time);
					if (wParam & MK_RBUTTON)
						setSubtitleCodeEmpty(time);
					Invalidate();
				} else if (mMouseAt == WAVEFORM_SLIDER && (wParam & MK_LBUTTON)) {
					double pos = (double)(mx + mWaveSliderScrollBase - mWaveSliderRect.left - mWaveSliderWidth) / (mWaveSliderRect.right - mWaveSliderRect.left - mWaveSliderWidth);
					pos = std::max(0., std::min(1., pos));
					Media *media = mMedia[mCurrentTab];
					int samples = media->GetAudioProperties()->NumSamples;
					samples -= (int)(samples / media->GetWaveView()->GetZoom());
					media->GetWaveView()->ScrollTo((int)(samples * pos));
					mWaveCurrentPosLeftScreen = media->GetWaveView()->GetX(media->GetAudioFrameIndex()) < 0;
					Invalidate(&mWaveRect);
					Invalidate(&mWaveSliderRect);
				} else if (mMouseAt == LIMITER_VIDEO && (wParam & MK_LBUTTON)) {
					mVideoMaxHeight = std::max(32, my);
					SendMessage(getHSelf(), WM_SIZE, 0, 0);
					Invalidate();
				} else if (mMouseAt == LIMITER_WAVEFORM && (wParam & MK_LBUTTON)) {
					mWaveMaxHeight = std::max(32, my - (int)mWaveRect.top);
					Invalidate();
				}
			}
			break;
		case WM_MOUSEWHEEL: {
			int mx = GET_X_LPARAM(lParam);
			int my = GET_Y_LPARAM(lParam);
			if (mMedia.count(mCurrentTab) > 0) {
				Media* media = mMedia[mCurrentTab];
				WaveView* wv = media->GetWaveView();
				if (wv != NULL) {
					RECT rc;
					GetWindowRect(getHSelf(), &rc);
					mx -= rc.left; 
					my -= rc.top;
					if (my > mWaveRect.top && my < mWaveRect.bottom &&
						mx > mWaveRect.left && mx < mWaveRect.right) {
						if (mMouseAt == NONE) {
							if (LOWORD(wParam) & MK_CONTROL)
								wv->Zoom(GET_WHEEL_DELTA_WPARAM(wParam) / (double)WHEEL_DELTA);
							else {
								wv->Scroll(GET_WHEEL_DELTA_WPARAM(wParam) / (double)WHEEL_DELTA);
								mWaveCurrentPosLeftScreen = media->GetWaveView()->GetX(media->GetAudioFrameIndex()) < 0;
							}
							Invalidate();
						}
					}
				}
			}
			break;
		}
		case WM_TIMER: {
			if (wParam == TRACKBAR_TIMER) {
				if (mMedia.count(mCurrentTab) > 0) {
					SendMessage(mControls.mPlaybackSlider, TBM_SETPOS, TRUE,
						(int)(mMedia[mCurrentTab]->GetTime()*mProgressMax / mMedia[mCurrentTab]->GetLength()));
					RECT rc;
					GetClientRect(mControls.mPlaybackSlider, &rc);
					rc.left = 10;
					rc.right -= 10;
					rc.top = mVideoRect.bottom + rc.bottom + 10;
					rc.bottom = rc.top + 200;
					Invalidate(&rc);
				}
			}
			break;
		}
		case WM_SIZE: {
			RECT r, r2;
			this->getClientRect(r);
			mVideoSizeLimiter = mVideoRect = { 0, 0, 0, 0 };
			if (mMedia.count(mCurrentTab) > 0) {
				Media *media = mMedia[mCurrentTab];
				BITMAPINFOHEADER binfo = media->GetBitmapInfo().bmiHeader;
				if (binfo.biWidth > 0 && binfo.biHeight > 0) {
					r.right -= r.left; r.left = 0;
					r.top = 0;
					r.bottom = r.right * binfo.biHeight / binfo.biWidth;
					if (r.bottom > mVideoMaxHeight) {
						r.right = r.right * mVideoMaxHeight / r.bottom;
						r.bottom = mVideoMaxHeight;
					}
					mVideoRect = r;
				}
				this->getClientRect(r);
				mVideoSizeLimiter = { r.left, mVideoRect.bottom, r.right, mVideoRect.bottom + mLimiterHeight }; // left top right bottom
			}
			GetWindowRect(mControls.mPlaybackSlider, &r2);
			SetWindowPos(mControls.mPlaybackSlider, NULL, 0, mVideoSizeLimiter.bottom, r.right, r2.bottom - r2.top, SWP_NOACTIVATE);
			GetWindowRect(mControls.mOpenCancel, &r2);
			SetWindowPos(mControls.mOpenProgress, NULL, 0, 0, r.right - (r2.right - r2.left), r2.bottom - r2.top, SWP_NOACTIVATE);
			SetWindowPos(mControls.mOpenCancel, NULL, r.right - (r2.right - r2.left), 0, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
			Invalidate();
			break;
		}
		case WM_ERASEBKGND: {
			RECT r;
			HDC hdc = (HDC)wParam;
			HBRUSH bg = (HBRUSH)(COLOR_BTNFACE + 1);
			this->getClientRect(r);
			if (r.right != mVideoRect.right) {
				r.top = 0;
				r.bottom = mVideoRect.bottom;
				r.left = mVideoRect.right;
				FillRect(hdc, &r, bg);
			}
			return true;
		}
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		case WM_NOTIFY: {
			bool nf = GetFocus() == getHSelf() || GetFocus() == mControls.mPlaybackSlider;
			if (m_focused == nf)
				break;
			m_focused = nf;
			RECT rc;
			GetClientRect(mControls.mPlaybackSlider, &rc);
			rc.left = 10;
			rc.right -= 10;
			rc.top = std::max(mWaveSliderRect.bottom, mVideoRect.bottom + rc.bottom) + 10;
			rc.bottom = rc.top + 600;
			Invalidate(&rc);
			break;
		}
		case WM_PAINT: {
			PAINTSTRUCT ps;
			RECT self, rc;
			HDC hdc;
			HBRUSH bg = (HBRUSH)(COLOR_BTNFACE + 1);
			GetClientRect(getHSelf(), &self);

			hdc = BeginPaint(getHSelf(), &ps);
			SetStretchBltMode(hdc, HALFTONE);
			SetBkMode(hdc, TRANSPARENT);

			HFONT font = (HFONT)SelectObject(hdc, CreateFont(15, 0, 0, 0, 400, false, false, false, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
				CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Segoe UI")));

			if (mMedia.count(mCurrentTab) > 0) {
				Media *media = mMedia[mCurrentTab];
				if (media->State() != STATE_CLOSED && media->HasVideo()) {
					const FFMS_Frame* frame = media->GetFrame();
					BITMAPINFO binfo = media->GetBitmapInfo();
					BITMAPINFOHEADER bh = binfo.bmiHeader;
					if (frame != NULL) {
						SetBrushOrgEx(hdc, 0, 0, NULL);
						StretchDIBits(hdc, 0, mVideoRect.bottom, mVideoRect.right, -mVideoRect.bottom,
							0, 0, bh.biWidth, bh.biHeight,
							frame->Data[0], &binfo, DIB_RGB_COLORS, SRCCOPY);
					}
				}
				self.top = mVideoSizeLimiter.bottom;
				{
					FillRect(hdc, &mVideoSizeLimiter, bg);
					FillRect(hdc, &mWaveSizeLimiter, bg);
				}

				{
					const FFMS_Frame* frame = media->HasVideo() ? media->GetFrame() : NULL;
					double t = media->GetTime();
					if (t < 0) t = 0;
					double t2 = media->GetLength();
					if (t2 < 0) t2 = 0;
					TCHAR msg[1024];
					GetClientRect(mControls.mPlaybackSlider, &rc);
					rc.top = self.top + rc.bottom;
					rc.bottom = rc.top + mWaveMaxHeight - mWaveScrollerHeight;
					rc.left = self.left;
					rc.right = self.right;
					if (media->RenderAudio(hdc, rc)) {
						mWaveRect = rc;
						int x = media->GetWaveView()->GetX(media->GetAudioFrameIndex());
						if (x < 0 && !mWaveCurrentPosLeftScreen)
							media->GetWaveView()->ScrollTo(media->GetAudioFrameIndex());
						else {
							HPEN pen = (HPEN)SelectObject(hdc, CreatePen(PS_SOLID, 1, RGB(0, 255, 0)));
							MoveToEx(hdc, x, mWaveRect.top, NULL);
							LineTo(hdc, x, mWaveRect.bottom);
							DeleteObject(SelectObject(hdc, pen));
						}
						rc.top = rc.bottom;
						rc.bottom = rc.top + mWaveScrollerHeight;
						mWaveSliderRect = rc;
						{
							mWaveSliderWidth = std::max(8, (int)((rc.right - rc.left) / media->GetWaveView()->GetZoom()));
							double gen = media->GetAudioWaveformGenerationStatus();
							HPEN pen = (HPEN)SelectObject(hdc, CreatePen(PS_SOLID, 1, RGB(0, 0, 255)));
							HBRUSH brush = (HBRUSH)SelectObject(hdc, CreateSolidBrush(0));
							Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
							DeleteObject(SelectObject(hdc, pen));
							DeleteObject(SelectObject(hdc, brush));
							brush = (HBRUSH)SelectObject(hdc, CreateSolidBrush(RGB(255, 255, 255)));
							int samples = media->GetAudioProperties()->NumSamples;
							samples -= (int)(samples / media->GetWaveView()->GetZoom());
							int sliderLeft = samples == 0 ? 0 : (int)((double)(rc.right - rc.left - mWaveSliderWidth) * media->GetWaveView()->GetScroll() / samples);
							Rectangle(hdc, rc.left + sliderLeft, rc.top, rc.left + sliderLeft + mWaveSliderWidth, rc.bottom);
							DeleteObject(SelectObject(hdc, brush));
							if (gen < 1) {
								brush = (HBRUSH)SelectObject(hdc, CreateSolidBrush(RGB(48, 48, 255)));
								Rectangle(hdc, rc.left, rc.bottom - 8, rc.left + (int)(gen * (rc.right - rc.left)), rc.bottom);
								DeleteObject(SelectObject(hdc, brush));
							}
						}
						mWaveSizeLimiter = { rc.left, rc.bottom, rc.right, rc.bottom + mLimiterHeight };

						rc.top = mWaveSizeLimiter.bottom + 10;
					} else {
						mWaveSizeLimiter = mWaveRect = { 0, 0, 0, 0 };
					}

					rc.bottom = self.bottom;
					FillRect(hdc, &rc, bg);
					rc.left = 10;
					rc.right -= 10;


					wsprintf(msg, L"          %s%I64d/%I64d\n"
						"%02d:%02d:%02d.%03d/%02d:%02d:%02d.%03d",
						frame == NULL || !frame->KeyFrame ? L"" : L"[K] ",
						media->GetFrameIndex(), media->GetFrameCount(),
						(int)(t / 3600), (int)(t / 60) % 60, (int)(t) % 60, (int)(t * 1000) % 1000,
						(int)(t2 / 3600), (int)(t2 / 60) % 60, (int)(t2) % 60, (int)(t2 * 1000) % 1000);
					DrawText(hdc, msg, wcslen(msg), &rc, DT_RIGHT | DT_EXTERNALLEADING | DT_WORDBREAK);
				}
				if (m_focused) {
					if (media->State() == PlaybackState::STATE_ERROR) {
						const char* err = media->GetErrorMsg();
						TextOutA(hdc, rc.left, rc.top, err, strlen(err));
						rc.top += 60;
					}
					{
						TCHAR *msg = L""
							"Left: -3 sec\n"
							"Right: +3 sec\n"
							"Ctrl+Left: -1 frame\n"
							"Ctrl+Right: +1 frame\n"
							"F9: Play / Pause\n"
							"Z, Num0: Undo\n"
							"X, Up: Insert timecode\n"
							"C, Down: Insert stop timecode";
						rc.bottom = rc.top + 600;
						DrawText(hdc, msg, wcslen(msg), &rc, DT_LEFT | DT_EXTERNALLEADING | DT_WORDBREAK);
					}
					{
						TCHAR *msg = L""
							"R: Play range\n"
							"Q: Play 500ms before\n"
							"W: Play 500ms after\n"
							"E: Play 500ms start\n"
							"D: Play 500ms end\n"
							"G: Next line\n"
							"S: Save";
						rc.left += 160;
						DrawText(hdc, msg, wcslen(msg), &rc, DT_LEFT | DT_EXTERNALLEADING | DT_WORDBREAK);
					}
				}
			}

			DeleteObject(SelectObject(hdc, font));
			EndPaint(getHSelf(), &ps);

			return true;
		}
		case WM_DESTROY: {
			break;
		}
		case WM_GRAPH_EVENT: {
			// TODO ??
			break;
		}
	}
	return DockingDlgInterface::run_dlgProc(message, wParam, lParam);
}

