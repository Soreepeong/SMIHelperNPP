//
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

extern FuncItem funcItem[nbFunc];
extern NppData nppData;
extern DWORD myMessageId;

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  reasonForCall,
	LPVOID lpReserved) {
	switch (reasonForCall) {
		case DLL_PROCESS_ATTACH:
			pluginInit(hModule);
			break;

		case DLL_PROCESS_DETACH:
			commandMenuCleanUp();
			pluginCleanUp();
			break;

		case DLL_THREAD_ATTACH:
			break;

		case DLL_THREAD_DETACH:
			break;
	}

	return TRUE;
}


extern "C" __declspec(dllexport) void setInfo(NppData notpadPlusData) {
	nppData = notpadPlusData;
	commandMenuInit();
}

extern "C" __declspec(dllexport) const TCHAR * getName() {
	return NPP_PLUGIN_NAME;
}

extern "C" __declspec(dllexport) FuncItem * getFuncsArray(int *nbF) {
	*nbF = nbFunc;
	return funcItem;
}


extern "C" __declspec(dllexport) void beNotified(SCNotification *notifyCode) {
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam) {
	if (Message == NPPM_MSGTOPLUGIN) {
		HandleMessages(lParam);
		return false;
	} else if (Message == WM_SIZE) {
		int n = SendMessage(nppData._nppHandle, NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES);
		if (n > 0) {
			TCHAR** files = new TCHAR*[n];
			for (int i = 0; i < n; i++)
				files[i] = new TCHAR[2048];
			SendMessage(nppData._nppHandle, NPPM_GETOPENFILENAMES, (WPARAM)files, n);
			std::vector<std::wstring> filenames;
			for (int i = 0; i < n; i++)
				filenames.push_back(std::wstring(files[i]));
			SendMessage(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, 2048, (LPARAM)files[0]);
			std::wstring current(files[0]);
			onTabChanged(current, filenames);
			for (int i = 0; i < n; i++)
				delete[] files[i];
			delete files;
		}
	}
	return TRUE;
}

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode() {
	return TRUE;
}
#endif //UNICODE
