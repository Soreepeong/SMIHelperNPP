//this file is part of notepad++
//Copyright (C)2003 Don HO <donho@altern.org>
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "PluginDefinition.h"
#include "menuCmdID.h"
#include "MpcHcRemote.h"
#include "SRTMaker.h"
#include "DockedPlayer.h"

bool bUseDockedPlayer;
DockedPlayer dockedPlayer;
DWORD myMessageId;

TCHAR iniFilePath[MAX_PATH];
TCHAR moduleName[MAX_PATH];
const TCHAR sectionName[] = TEXT("SMIHelper");
const TCHAR KEY_USE_DOCKED_PLAYER[] = TEXT("useDockedPlayer");
const TCHAR configFileName[] = TEXT("SMIHelper.ini");

FuncItem funcItem[nbFunc];
NppData nppData;

bool isSMIfile() {
	TCHAR filename[MAX_PATH];
	::SendMessage(nppData._nppHandle, NPPM_GETFILENAME, sizeof(filename), (WPARAM)filename);
	int filename_len = wcslen(filename);
	wcslwr(filename);
	return filename_len >= 4 && wcscmp(filename + filename_len - 4, TEXT(".smi")) == 0;
}

DWORD WINAPI HandleMessages(LPARAM lParam) {
	switch ((int)lParam) {
		case VK_F5: insertSubtitleCode(); break;
		case VK_F6: insertSubtitleCodeEmpty(); break;
		case VK_F9: playerPlayPause(); break;
		case 'R':
		case VK_SPACE:
		case VK_F10: playerPlayRange(); break;
		case VK_F8: playerJumpTo(); break;
		case VK_F7: gotoCurrentLine(); break;
		case VK_LEFT:
			if (GetAsyncKeyState(VK_CONTROL) && dockedPlayer.IsFocused())
				playerRewFrame();
			else
				playerRew();
			break;
		case VK_RIGHT:
			if (GetAsyncKeyState(VK_CONTROL) && dockedPlayer.IsFocused())
				playerFFFrame();
			else
				playerFF();
			break;
		case 'X':
		case VK_UP:
			insertSubtitleCode();
			break;
		case 'C':
		case VK_DOWN:
			insertSubtitleCodeEmpty();
			break;
		case 'Z': 
		case VK_NUMPAD0: {
			int which = -1;
			::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
			if (which == -1)
				break;
			HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
			::SendMessage(curScintilla, SCI_UNDO, 0, 0);
			::SendMessage(curScintilla, SCI_LINESCROLL, 0, -1);
			break;
		}
		case 'S': {
			::SendMessage(nppData._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
			break;
		}
		case 'Q': playerPlayRangeInternal(-0.5, 0); break;
		case 'E': playerPlayRangeInternal(0.5, 0); break;
		case 'D': playerPlayRangeInternal(0, -0.5); break;
		case 'W': playerPlayRangeInternal(0, 0.5); break;
		case 'G': selectNextLine(); break;
	}
	return 0;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if ((GetForegroundWindow() == nppData._nppHandle || dockedPlayer.IsFocused()) && nCode == HC_ACTION) {
		if (isSMIfile()) {
			switch (wParam) {
				case WM_KEYDOWN:
				case WM_SYSKEYDOWN:
				case WM_KEYUP:
				case WM_SYSKEYUP:
					PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;
					if(VK_F5 <= p->vkCode && p->vkCode <= VK_F10) {
						if (wParam == WM_KEYDOWN) {
							PostMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM)moduleName, (LPARAM)p->vkCode);
							return 1;
						}
					} else if ('A' <= p->vkCode && p->vkCode <= 'Z') {
						if (dockedPlayer.IsFocused()) {
							if (wParam == WM_KEYDOWN)
								PostMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM)moduleName, (LPARAM)p->vkCode);
							return 1;
						}
					}else switch (p->vkCode) {
						case VK_SPACE:
						case VK_LEFT:
						case VK_RIGHT: {
							if (dockedPlayer.IsFocused() || (GetAsyncKeyState(VK_CONTROL) && GetAsyncKeyState(VK_MENU) && wParam == WM_KEYDOWN)) {
								PostMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM)moduleName, (LPARAM)p->vkCode);
								return 1;
							}
							break;
						}
						case VK_NUMPAD0:
						case VK_UP:
						case VK_DOWN:{
							if (dockedPlayer.IsFocused()) {
								if (wParam == WM_KEYDOWN)
									PostMessage(nppData._nppHandle, NPPM_MSGTOPLUGIN, (WPARAM)moduleName, (LPARAM)p->vkCode);
								return 1;
							}
							break;
						}
					}
			}
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
HHOOK hhkLowLevelKybd;

void pluginInit(HANDLE hModule) {
	WSADATA w;
	WSAStartup((MAKEWORD(2, 2)), &w);
	hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
	CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
	GetModuleFileName((HMODULE) hModule, moduleName, sizeof(moduleName));
	wcscpy(moduleName, wcsrchr(moduleName, '\\') + 1);

	dockedPlayer.init((HINSTANCE)hModule, NULL);
}
void pluginCleanUp() {
	TCHAR kc[3];
	int k = (bUseDockedPlayer ? 1 : 0) | (dockedPlayer.isClosed() ? 0 : 2);
	wsprintf(kc, L"%d", k);
	::WritePrivateProfileString(sectionName, KEY_USE_DOCKED_PLAYER, kc, iniFilePath);
	UnhookWindowsHookEx(hhkLowLevelKybd);
	WSACleanup();
}

void commandMenuInit() {
	SendMessage(nppData._nppHandle, NPPM_ALLOCATECMDID, 1, (LPARAM) &myMessageId);
	dockedPlayer.setParent(nppData._nppHandle);


	SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, sizeof(iniFilePath), (LPARAM)iniFilePath);
	if (PathFileExists(iniFilePath) == FALSE)
		::CreateDirectory(iniFilePath, NULL);

	PathAppend(iniFilePath, configFileName);

	bUseDockedPlayer = ((::GetPrivateProfileInt(sectionName, KEY_USE_DOCKED_PLAYER, 0, iniFilePath) & 1) != 0);


	setCommand(0, TEXT("Insert Timecode"), insertSubtitleCode, (new ShortcutKey)->set(false, false, false, VK_F5), false);
	setCommand(1, TEXT("Insert Hiding Timecode"), insertSubtitleCodeEmpty, (new ShortcutKey)->set(false, false, false, VK_F6), false);
	setCommand(2, TEXT("---"), NULL, NULL, false);
	setCommand(3, TEXT("Use Docked Player"), useDockedPlayer, (new ShortcutKey)->set(true, true, false, 'P'), MF_BYCOMMAND | (bUseDockedPlayer ? MF_CHECKED : MF_UNCHECKED));
	setCommand(4, TEXT("Reopen file"), playerClose, (new ShortcutKey)->set(true, true, false, 'O'), false);
	setCommand(5, TEXT("Play/Pause"), playerPlayPause, (new ShortcutKey)->set(false, false, false, VK_F9), false);
	setCommand(6, TEXT("Play Range"), playerPlayRange, (new ShortcutKey)->set(false, false, false, VK_F10), false);
	setCommand(7, TEXT("Go to current line"), playerJumpTo, (new ShortcutKey)->set(false, false, false, VK_F8), false);
	setCommand(8, TEXT("Rewind"), playerRew, (new ShortcutKey)->set(true, true, false, VK_LEFT), false);
	setCommand(9, TEXT("Fast Forward"), playerFF, (new ShortcutKey)->set(true, true, false, VK_RIGHT), false);
	setCommand(10, TEXT("Move line to current time"), gotoCurrentLine, (new ShortcutKey)->set(false, false, false, VK_F7), false);
	setCommand(11, TEXT("---"), NULL, NULL, false);
	setCommand(12, TEXT("Add template"), addTemplate, NULL, false);
	setCommand(13, TEXT("Make SRT"), makeSRT, NULL, false);
	setCommand(14, TEXT("Make ASS"), makeASS, NULL, false);
	if (!bUseDockedPlayer)
		useDockedPlayer();
}
void commandMenuCleanUp() {
	for (int i = 0; i < nbFunc; i++)
		delete funcItem[i]._pShKey;
}


bool setCommand(size_t index, TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk, int check0nInit) {
	if (index >= nbFunc)
		return false;

	if (!pFunc)
		return false;

	lstrcpy(funcItem[index]._itemName, cmdName);
	funcItem[index]._pFunc = pFunc;
	funcItem[index]._init2Check = 0 != check0nInit;
	funcItem[index]._pShKey = sk;

	return true;
}

void onTabChanged(std::wstring selection, std::vector<std::wstring> &newlist) {
	std::vector<std::wstring> &openfiles = dockedPlayer.GetOpenIds();
	for (std::vector<std::wstring>::iterator i = openfiles.begin(); i != openfiles.end(); ++i) {
		if (std::find(newlist.begin(), newlist.end(), *i) == newlist.end())
			dockedPlayer.CloseTab(*i);
	}
	dockedPlayer.SetTab(selection);
}

void tryOpenMedia() {
	TCHAR szFile[MAX_PATH] = { 0, };
	TCHAR basepath[MAX_PATH + 3] = { 0, };
	TCHAR *szMediaTypes = TEXT("3G2;3GP;3GP2;3GPP;AMV;ASF;AVI;AVS;DIVX;EVO;F4V;FLV;GVI;HDMOV;IFO;K3G;M2T;M2TS;MKV;MK3D;MOV;MP2V;MP4;MPE;MPEG;MPG;MPV2;MQV;MTS;MTV;NSV;OGM;OGV;QT;RM;RMVB;RV;SKM;TP;TPR;TS;VOB;WEBM;WM;WMP;WMV;A52;AAC;AC3;AIF;AIFC;AIFF;ALAC;AMR;APE;AU;CDA;DTS;FLA;FLAC;M1A;M2A;M4A;M4B;M4P;MID;MKA;MP1;MP2;MP3;MPA;MPC;MPP;MP+;NSA;OFR;OFS;OGA;OGG;RA;SND;SPX;TTA;WAV;WAVE;WMA;WV");
	OPENFILENAME ofn;
	TCHAR mpchc[MAX_PATH];
	DWORD len = MAX_PATH;
	DWORD res = 0;

	if (!bUseDockedPlayer) {
		if (NULL != RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\MPC-HC\\MPC-HC"), TEXT("ExePath"), 0x00010000 | RRF_RT_REG_SZ, NULL, &mpchc, &len)) {
			MessageBox(nppData._nppHandle, TEXT("MPC-HC not found."), TEXT("SMI Helper Error"), MB_ICONERROR);
			return;
		}
		if (NULL != RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\MPC-HC\\MPC-HC\\Settings"), TEXT("EnableWebServer"), 0x00010000 | RRF_RT_REG_DWORD, NULL, &res, &len) || res == 0) {
			MessageBox(nppData._nppHandle, TEXT("Web Server feature of MPC-HC is inactive."), TEXT("SMI Helper Error"), MB_ICONERROR);
			return;
		}
	}
	::SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (WPARAM)basepath);
	TCHAR *pos = wcsrchr(basepath, L'.');
	if (pos != NULL) {
		wsprintf(pos, L".*");
		WIN32_FIND_DATA file;
		HANDLE hList = FindFirstFile(basepath, &file);
		if (hList != INVALID_HANDLE_VALUE) {
			do {
				if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					wcslwr(file.cFileName);
					pos = wcsrchr(file.cFileName, L'.');
					if (pos != NULL) {
						pos++;
						wcsupr(pos);
						if (wcsstr(szMediaTypes, pos) != NULL) {
							::SendMessage(nppData._nppHandle, NPPM_GETCURRENTDIRECTORY, MAX_PATH, (WPARAM)szFile);
							int len = wcslen(szFile);
							if (szFile[len - 1] != TEXT('\\'))
								wsprintf(szFile + len++, TEXT("\\"));
							wcsncpy(szFile + len, file.cFileName, min(wcslen(file.cFileName), min(sizeof(szFile), sizeof(file.cFileName))));
							break;
						}
					}
				}
			} while (FindNextFile(hList, &file));
			FindClose(hList);
		}
	}
	if (szFile[0] == NULL) {
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = nppData._nppHandle;
		ofn.lpstrTitle = TEXT("Select media file to use with");
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = TEXT("All Video\0*.3G2;*.3GP;*.3GP2;*.3GPP;*.AMV;*.ASF;*.AVI;*.AVS;*.DIVX;*.EVO;*.F4V;*.FLV;*.GVI;*.HDMOV;*.IFO;*.K3G;*.M2T;*.M2TS;*.MKV;*.MK3D;*.MOV;*.MP2V;*.MP4;*.MPE;*.MPEG;*.MPG;*.MPV2;*.MQV;*.MTS;*.MTV;*.NSV;*.OGM;*.OGV;*.QT;*.RM;*.RMVB;*.RV;*.SKM;*.TP;*.TPR;*.TS;*.VOB;*.WEBM;*.WM;*.WMP;*.WMV\0All Audio\0*.A52;*.AAC;*.AC3;*.AIF;*.AIFC;*.AIFF;*.ALAC;*.AMR;*.APE;*.AU;*.CDA;*.DTS;*.FLA;*.FLAC;*.M1A;*.M2A;*.M4A;*.M4B;*.M4P;*.MID;*.MKA;*.MP1;*.MP2;*.MP3;*.MPA;*.MPC;*.MPP;*.MP+;*.NSA;*.OFR;*.OFS;*.OGA;*.OGG;*.RA;*.SND;*.SPX;*.TTA;*.WAV;*.WAVE;*.WMA;*.WV\0All Media\0*.3G2;*.3GP;*.3GP2;*.3GPP;*.AMV;*.ASF;*.AVI;*.AVS;*.DIVX;*.EVO;*.F4V;*.FLV;*.GVI;*.HDMOV;*.IFO;*.K3G;*.M2T;*.M2TS;*.MKV;*.MK3D;*.MOV;*.MP2V;*.MP4;*.MPE;*.MPEG;*.MPG;*.MPV2;*.MQV;*.MTS;*.MTV;*.NSV;*.OGM;*.OGV;*.QT;*.RM;*.RMVB;*.RV;*.SKM;*.TP;*.TPR;*.TS;*.VOB;*.WEBM;*.WM;*.WMP;*.WMV;*.A52;*.AAC;*.AC3;*.AIF;*.AIFC;*.AIFF;*.ALAC;*.AMR;*.APE;*.AU;*.CDA;*.DTS;*.FLA;*.FLAC;*.M1A;*.M2A;*.M4A;*.M4B;*.M4P;*.MID;*.MKA;*.MP1;*.MP2;*.MP3;*.MPA;*.MPC;*.MPP;*.MP+;*.NSA;*.OFR;*.OFS;*.OGA;*.OGG;*.RA;*.SND;*.SPX;*.TTA;*.WAV;*.WAVE;*.WMA;*.WV\0All Files\0*.*\0");
		ofn.nFilterIndex = 3;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		::SendMessage(nppData._nppHandle, NPPM_GETCURRENTDIRECTORY, sizeof(basepath), (WPARAM)basepath);
		ofn.lpstrInitialDir = basepath;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
		if (GetOpenFileName(&ofn) != TRUE)
			return;
	}
	if (szFile[0] == NULL)
		return;
	if (bUseDockedPlayer) {
		dockedPlayer.OpenFile(szFile);
		dockedPlayer.display(true);
	} else {
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		TCHAR cmd[MAX_PATH * 2];

		wsprintf(cmd, TEXT("\"%s\" \"%s\""), mpchc, szFile);
		if (!CreateProcess(mpchc, cmd, NULL, NULL, false, NULL, NULL, NULL, &si, &pi)) {
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
	}
}

void playerClose() {
	if (dockedPlayer.GetMedia() != NULL && 
		dockedPlayer.GetMedia()->State() == PlaybackState::STATE_RUNNING) {
		playerPlayPause();
		Sleep(50);
	}
	tryOpenMedia();
}

void insertSubtitleCode() {
	// Get the current scintilla
	int which = -1;
	char sync[8192];
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;

	int time = bUseDockedPlayer ? (dockedPlayer.GetMedia() == NULL ? -1 : (int)(dockedPlayer.GetMedia()->GetTime() * 1000)) : getMpcHcTime();

	if (time == -1) {
		tryOpenMedia();
		return;
	}

	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	::SendMessage(curScintilla, SCI_BEGINUNDOACTION, 0, 0);
	LockWindowUpdate(nppData._nppHandle);

	::SendMessage(curScintilla, SCI_HOME, 0, 0);
	bool replaced = false;
	int curLine = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, ::SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0);
	int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
	if (lineLength - 1 <= sizeof(sync)) {
		::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
		sync[lineLength] = 0;
		std::smatch match;
		std::string subject(sync);
		if (std::regex_search(subject, match, syncmatcher) && match.size() > 1) {
			::SendMessage(curScintilla, SCI_SETANCHOR, match.str(0).length() + ::SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine, 0), 0);
			snprintf(sync, 512, "<Sync Start=%d>", time);
			::SendMessage(curScintilla, SCI_REPLACESEL, NULL, (LPARAM)sync);
			replaced = true;
		}
	}
	if (!replaced) {
		snprintf(sync, 512, "<Sync Start=%d><P>", time);
		::SendMessage(curScintilla, SCI_ADDTEXT, strlen(sync), (LPARAM)sync);
	}

	int lineCount = ::SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0);
	if (curLine == lineCount - 1) { // last line?
		::SendMessage(curScintilla, SCI_LINEEND, 0, 0);
		::SendMessage(curScintilla, SCI_ADDTEXT, 2, (LPARAM)"\r\n");
	} else
		::SendMessage(curScintilla, SCI_GOTOLINE, curLine + 1, 0);
	if(!replaced)
		::SendMessage(curScintilla, SCI_LINESCROLL, 0, 1);
	::SendMessage(curScintilla, SCI_ENDUNDOACTION, 0, 0);
	LockWindowUpdate(NULL);
}

void setSubtitleCode(double time_s) {
	// Get the current scintilla
	int which = -1;
	char sync[8192];
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;

	int time = (int)(time_s * 1000);

	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	bool replace = false;
	int curLine = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, ::SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0);
	int selLength = 0, selStart = 0;
	do {
		int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
		if (lineLength - 1 > sizeof(sync))
			continue;
		::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
		sync[lineLength] = 0;
		std::smatch match;
		std::string subject(sync);
		if (std::regex_search(subject, match, syncmatcher) && match.size() >= 1) {
			/*
			subject = std::regex_replace(subject, tag_remover, "");
			subject = std::regex_replace(subject, space_remover, "");
			if (subject != "&nbsp;")
				replace = true;
			//*/
			selLength = match.length();
			selStart = SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine, 0);
			replace = true;
			break;
		}
	} while (--curLine > 0);

	::SendMessage(curScintilla, SCI_SETTARGETRANGE, selStart, selStart + selLength);
	snprintf(sync, 512, "<Sync Start=%d>", time);
	::SendMessage(curScintilla, SCI_REPLACETARGET, -1, (LPARAM)sync);
}

void setSubtitleCodeEmpty(double time_s) {
	// Get the current scintilla
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;

	int time = (int)(time_s * 1000);

	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	char sync[2048];
	int linec = ::SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0);
	int curLine = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, ::SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0) + 1;
	int selLength = 0, selStart = SendMessage(curScintilla, SCI_POSITIONFROMLINE, linec - 1, 0);
	bool replace = false;
	do {
		int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
		if (lineLength - 1 > sizeof(sync))
			continue;
		::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
		sync[lineLength] = 0;
		std::smatch match;
		std::string subject(sync);
		if (std::regex_search(subject, match, syncmatcher) && match.size() >= 1) {
			selStart = SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine, 0);
			subject.erase(subject.begin(), subject.begin() + match.length());
			subject = std::regex_replace(subject, tag_remover, "");
			subject = std::regex_replace(subject, space_remover, "");
			if (subject == "&nbsp;") {
				selLength = match.length();
				replace = true;
			}
			break;
		}
	} while (++curLine < linec);

	::SendMessage(curScintilla, SCI_SETTARGETRANGE, selStart, selStart + selLength);
	if(replace)
		snprintf(sync, 512, "<Sync Start=%d>", time);
	else
		snprintf(sync, 512, "<Sync Start=%d><P>&nbsp;\r\n", time);
	::SendMessage(curScintilla, SCI_REPLACETARGET, -1, (LPARAM)sync);
}

void selectNextLine() {
	// Get the current scintilla
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;

	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	char sync[2048];
	int linec = ::SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0);
	int curLine = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, ::SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0) + 1;
	do {
		int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
		if (lineLength - 1 > sizeof(sync))
			continue;
		::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
		sync[lineLength] = 0;
		std::smatch match;
		std::string subject(sync);
		if (std::regex_search(subject, match, syncmatcher) && match.size() >= 1) {
			subject.erase(subject.begin(), subject.begin() + match.length());
			subject = std::regex_replace(subject, tag_remover, "");
			subject = std::regex_replace(subject, space_remover, "");
			if (subject != "&nbsp;") {
				int selStart = SendMessage(curScintilla, SCI_POSITIONFROMLINE, curLine, 0);
				SendMessage(curScintilla, SCI_SETEMPTYSELECTION, selStart, 0);
				SendMessage(curScintilla, SCI_SCROLLCARET, 0, 0);
				break;
			}
		}
	} while (++curLine < linec);
}

void createUndoPoint(bool begin) {
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	if(begin)
		::SendMessage(curScintilla, SCI_BEGINUNDOACTION, 0, 0);
	else
		::SendMessage(curScintilla, SCI_ENDUNDOACTION, 0, 0);
}


void insertSubtitleCodeEmpty() {
	// Get the current scintilla
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;

	int time = bUseDockedPlayer ? (dockedPlayer.GetMedia() == NULL ? -1 : (int)(dockedPlayer.GetMedia()->GetTime() * 1000)) : getMpcHcTime();

	if (time == -1) {
		tryOpenMedia();
		return;
	}

	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	// Scintilla control has no Unicode mode, so we use (char *) here
	char test[512];
	snprintf(test, 512, "<Sync Start=%d><P>&nbsp;\r\n", time);
	::SendMessage(curScintilla, SCI_BEGINUNDOACTION, 0, 0);
	::SendMessage(curScintilla, SCI_HOME, 0, 0);
	::SendMessage(curScintilla, SCI_ADDTEXT, strlen(test), (LPARAM)test);
	::SendMessage(curScintilla, SCI_LINESCROLL, 0, 1);
	::SendMessage(curScintilla, SCI_ENDUNDOACTION, 0, 0);
}

void playerJumpTo() {
	int which = -1;
	char sync[8192];
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;

	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	int curLine = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, ::SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0);
	do {
		int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
		if (lineLength - 1 > sizeof(sync))
			continue;
		::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
		sync[lineLength] = 0;
		std::smatch match;
		std::string subject(sync);
		if (std::regex_search(subject, match, syncmatcher) && match.size() > 1) {
			if (bUseDockedPlayer) {
				if (dockedPlayer.GetMedia() == NULL)
					tryOpenMedia();
				else
					dockedPlayer.GetMedia()->SetTime(atoi(match.str(2).c_str()) / 1000.);
			} else {
				if (0 != seekMpcHc(atoi(match.str(2).c_str())))
					tryOpenMedia();
			}
			break;
		}
	} while (curLine-- > 0);
}

void gotoCurrentLine() {
	int which = -1;
	char sync[8192];
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	int time = bUseDockedPlayer ? (dockedPlayer.GetMedia() == NULL ? -1 : (int)(dockedPlayer.GetMedia()->GetTime() * 1000)) : getMpcHcTime();

	if (time == -1) {
		tryOpenMedia();
		return;
	}

	int curLine = ::SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0) - 1;
	do {
		int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
		if (lineLength - 1 > sizeof(sync))
			continue;
		::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
		sync[lineLength] = 0;
		std::smatch match;
		std::string subject(sync);
		if (std::regex_search(subject, match, syncmatcher) && match.size() > 1) {
			if (atoi(match.str(2).c_str()) < time) {
				::SendMessage(curScintilla, SCI_GOTOLINE, curLine + 1, 0);
				break;
			}
		}
	} while (curLine-- > 0);
}

void playerPlayPause() {
	if (bUseDockedPlayer) {
		if (dockedPlayer.GetMedia() == NULL)
			tryOpenMedia();
		else
			switch (dockedPlayer.GetMedia()->State()) {
				case PlaybackState::STATE_RUNNING:
					dockedPlayer.GetMedia()->Pause();
					break;
				case PlaybackState::STATE_PAUSED:
					dockedPlayer.GetMedia()->Play();
					break;
				case PlaybackState::STATE_OPENING:
					break;
				default:
					tryOpenMedia();
			}
	} else {
		if (0 != sendMpcHcCommand(889))
			tryOpenMedia();
	}
}

void playerPlayRangeInternal(double begin, double end) {
	if (bUseDockedPlayer) {
		if (dockedPlayer.GetMedia() == NULL)
			tryOpenMedia();
		else
			switch (dockedPlayer.GetMedia()->State()) {
				case PlaybackState::STATE_PAUSED:
				case PlaybackState::STATE_RUNNING: {
					int which = -1;
					char sync[8192];
					::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
					if (which == -1)
						return;
					HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
					double s1 = -1, s2 = -1;
					int linec = ::SendMessage(curScintilla, SCI_GETLINECOUNT, 0, 0);
					int curLine = ::SendMessage(curScintilla, SCI_LINEFROMPOSITION, ::SendMessage(curScintilla, SCI_GETCURRENTPOS, 0, 0), 0);
					do {
						int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
						if (lineLength - 1 > sizeof(sync))
							continue;
						::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
						sync[lineLength] = 0;
						std::smatch match;
						std::string subject(sync);
						if (std::regex_search(subject, match, syncmatcher) && match.size() > 1) {
							s1 = atoi(match.str(2).c_str()) / 1000.;
							break;
						}
					} while (curLine-- > 0);
					while (++curLine < linec) {
						int lineLength = ::SendMessage(curScintilla, SCI_LINELENGTH, curLine, 0);
						if (lineLength - 1 > sizeof(sync))
							continue;
						::SendMessage(curScintilla, SCI_GETLINE, curLine, (WPARAM)sync);
						sync[lineLength] = 0;
						std::smatch match;
						std::string subject(sync);
						if (std::regex_search(subject, match, syncmatcher) && match.size() > 1) {
							s2 = atoi(match.str(2).c_str()) / 1000.;
							break;
						}
					}
					if (s2 < 0)
						s2 = dockedPlayer.GetMedia()->GetLength();
					if (s1 >= 0 && s2 >= 0) {
						dockedPlayer.GetMedia()->Pause();
						if (begin < 0) {
							s2 = s1;
							s1 += begin;
						}else if(begin > 0){
							s2 = s1 + begin;
						} else if (end < 0) {
							s1 = s2 + end;
						} else if (end > 0) {
							s1 = s2;
							s2 += end;
						}
						dockedPlayer.GetMedia()->PlayRange(s1, s2);
					}
					break;
				}
				case PlaybackState::STATE_OPENING:
					break;
				default:
					tryOpenMedia();
			}
	} else {
		playerPlayPause();
	}
}

void playerPlayRange() {
	playerPlayRangeInternal(0, 0);
}

void playerRew() {
	if (bUseDockedPlayer) {
		if (dockedPlayer.GetMedia() == NULL || dockedPlayer.GetMedia()->State() == PlaybackState::STATE_ERROR)
			tryOpenMedia();
		else {
			dockedPlayer.GetMedia()->SetTime(dockedPlayer.GetMedia()->GetTime() - 3);
			dockedPlayer.GetMedia()->Play();
		}
	} else {
		if (0 != seekMpcHc(getMpcHcTime() - 3000))
			tryOpenMedia();
		else
			sendMpcHcCommand(887);
	}
}

void playerRewFrame() {
	if (bUseDockedPlayer) {
		if (dockedPlayer.GetMedia() == NULL || dockedPlayer.GetMedia()->State() == PlaybackState::STATE_ERROR)
			tryOpenMedia();
		else {
			dockedPlayer.GetMedia()->Pause();
			dockedPlayer.GetMedia()->SetFrameIndex(dockedPlayer.GetMedia()->GetFrameIndex() - 1);
		}
	} else {
		if (0 != sendMpcHcCommand(891))
			tryOpenMedia();
	}
}

void playerFF() {
	if (bUseDockedPlayer) {
		if (dockedPlayer.GetMedia() == NULL || dockedPlayer.GetMedia()->State() == PlaybackState::STATE_ERROR)
			tryOpenMedia();
		else {
			dockedPlayer.GetMedia()->SetTime(dockedPlayer.GetMedia()->GetTime() + 3);
			dockedPlayer.GetMedia()->Play();
		}
	} else {
		if (0 != seekMpcHc(getMpcHcTime() + 3000))
			tryOpenMedia();
		else
			sendMpcHcCommand(887);
	}
}

void playerFFFrame() {
	if (bUseDockedPlayer) {
		if (dockedPlayer.GetMedia() == NULL || dockedPlayer.GetMedia()->State() == PlaybackState::STATE_ERROR)
			tryOpenMedia();
		else {
			dockedPlayer.GetMedia()->Pause();
			dockedPlayer.GetMedia()->SetFrameIndex(dockedPlayer.GetMedia()->GetFrameIndex() + 1);
		}
	} else {
		if (0 != sendMpcHcCommand(892))
			tryOpenMedia();
	}
}

void addTemplate() {
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

	::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"<SAMI><HEAD></HEAD><BODY>\r\n");
	::SendMessage(curScintilla, SCI_INSERTTEXT, ::SendMessage(curScintilla, SCI_GETTEXTLENGTH, 0, 0), (LPARAM)"\r\n\r\n</BODY></SAMI>");
}

void makeSRT() {
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	std::string *res = convertSMItoSRT((char*)::SendMessage(curScintilla, SCI_GETCHARACTERPOINTER, 0, 0));
	if (res == NULL) {
		return;
	}

	// Open a new document
	::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
	which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	::SendMessage(curScintilla, SCI_SETTEXT, 0, (LPARAM)res->c_str());
	delete res;
}

void makeASS() {
	int which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	std::string *res = convertSMItoASS((char*)::SendMessage(curScintilla, SCI_GETCHARACTERPOINTER, 0, 0));
	if (res == NULL) {
		return;
	}

	// Open a new document
	::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
	which = -1;
	::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
	if (which == -1)
		return;
	curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	::SendMessage(curScintilla, SCI_SETTEXT, 0, (LPARAM)res->c_str());
	delete res;
}

void useDockedPlayer() {
	tTbData	data = { 0 };
	if (!dockedPlayer.isCreated()) {
		dockedPlayer.create(&data);

		// define the default docking behaviour
		data.uMask = DWS_DF_CONT_LEFT;

		data.pszModuleName = dockedPlayer.getPluginFileName();

		// the dlgDlg should be the index of funcItem where the current function pointer is
		// in this case is DOCKABLE_DEMO_INDEX
		data.dlgID = 3;
		::SendMessage(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&data);
		dockedPlayer.display(((::GetPrivateProfileInt(sectionName, KEY_USE_DOCKED_PLAYER, 0, iniFilePath) & 2) != 0) && bUseDockedPlayer);
	} else {
		if (!bUseDockedPlayer || !dockedPlayer.isClosed())
			bUseDockedPlayer = !bUseDockedPlayer;
		::CheckMenuItem(::GetMenu(nppData._nppHandle), funcItem[3]._cmdID, MF_BYCOMMAND | (bUseDockedPlayer ? MF_CHECKED : MF_UNCHECKED));
		if (bUseDockedPlayer) {
			dockedPlayer.display(true);
		} else
			dockedPlayer.display(false);
	}
}