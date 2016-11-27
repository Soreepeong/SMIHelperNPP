#include "DockedPlayer.h"
#include "PluginInterface.h"
#include <Commctrl.h>
#include <Mmsystem.h>
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
	InvalidateRect(getHSelf(), NULL, true);
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
	if (mMedia.count(mCurrentTab) > 0)
		delete mMedia[mCurrentTab];
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
	InvalidateRect(getHSelf(), &mVideoRect, false);
}

void DockedPlayer::SetTrackbarPosition(DockedPlayer::Media *media, int position) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	PostMessage(mControls.mPlaybackSlider, TBM_SETPOS, TRUE, position);
}

void DockedPlayer::StartTrackbarUpdate(DockedPlayer::Media *media) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	SetTimer(getHSelf(), TRACKBAR_TIMER, 250, NULL);
}

void DockedPlayer::StopTrackbarUpdate(DockedPlayer::Media *media) {
	if (mMedia.count(mCurrentTab) == 0 || mMedia[mCurrentTab] != media)
		return;
	KillTimer(getHSelf(), TRACKBAR_TIMER);
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
		case WM_TIMER: {
			if (wParam == TRACKBAR_TIMER) {
				if (mMedia.count(mCurrentTab) > 0)
					SendMessage(mControls.mPlaybackSlider, TBM_SETPOS, TRUE,
						(int)(mMedia[mCurrentTab]->GetTime()*mProgressMax / mMedia[mCurrentTab]->GetLength()));
			}
			break;
		}
		case WM_SIZE: {
			RECT r, r2;
			this->getClientRect(r);
			mVideoRect = { 0, 0, 0, 0 };
			if (mMedia.count(mCurrentTab) > 0) {
				BITMAPINFOHEADER binfo = mMedia[mCurrentTab]->GetBitmapInfo().bmiHeader;
				if (binfo.biWidth > 0 && binfo.biHeight > 0) {
					r.right -= r.left; r.left = 0;
					r.top = 0;
					r.bottom = r.right * binfo.biHeight / binfo.biWidth;
					mVideoRect = r;
				}
			}
			GetWindowRect(mControls.mPlaybackSlider, &r2);
			SetWindowPos(mControls.mPlaybackSlider, NULL, 0, mVideoRect.bottom, r.right, r2.bottom - r2.top, SWP_NOACTIVATE);
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
			r.top = mVideoRect.bottom;
			FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1));
			return true;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			SetFocus(getHSelf());
			break;
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
			rc.top = mVideoRect.bottom + rc.bottom + 10;
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

			if (mMedia.count(mCurrentTab) > 0) {
				Media *media = mMedia[mCurrentTab];
				if (media->State() != STATE_CLOSED && media->HasVideo()) {
					const FFMS_Frame* frame = media->GetFrame();
					BITMAPINFO binfo = media->GetBitmapInfo();
					BITMAPINFOHEADER bh = binfo.bmiHeader;
					if (frame != NULL) {
						SetStretchBltMode(hdc, HALFTONE);
						SetBrushOrgEx(hdc, 0, 0, NULL);
						StretchDIBits(hdc, 0, mVideoRect.bottom, self.right, -mVideoRect.bottom, 
							0, 0, bh.biWidth, bh.biHeight,
							frame->Data[0], &binfo, DIB_RGB_COLORS, SRCCOPY);
						double t = media->GetTime();
						if (t < 0) t = 0;
						TCHAR msg[1024];
						wsprintf(msg, L"%06I64d %02d:%02d:%02d.%03d", 
							media->GetFrameIndex(), 
							(int)(t / 3600), (int)(t / 60) % 60, (int)(t) % 60, (int)(t * 1000) % 1000);
						TextOut(hdc, 10, 10, msg, wcslen(msg));
					}
				}
				self.top = mVideoRect.bottom;
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
					rc.top = mVideoRect.bottom + rc.bottom + 10;
					if (media->State() == PlaybackState::STATE_ERROR) {
						const char* err = media->GetErrorMsg();
						TextOutA(hdc, rc.left, rc.top, err, strlen(err));
						rc.top += 60;
					}
					rc.bottom = rc.top + 600;
					DrawText(hdc, msg, wcslen(msg), &rc, DT_LEFT | DT_EXTERNALLEADING | DT_WORDBREAK);
				}
			}

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

