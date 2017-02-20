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

#ifndef PLUGINDEFINITION_H
#define PLUGINDEFINITION_H
#include "PluginInterface.h"

const TCHAR NPP_PLUGIN_NAME[] = TEXT("SMIHelper");

void pluginInit(HANDLE hModule);
void pluginCleanUp();
void commandMenuInit();
void commandMenuCleanUp();
bool setCommand(size_t index, TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk = NULL, int check0nInit = false);

DWORD WINAPI HandleMessages(LPARAM lParam);

void onTabChanged(std::wstring selection, std::vector<std::wstring> &newlist);

//
// Your plugin command functions
//
const std::regex syncmatcher("<sync(?=\\s)[^<>]*\\sstart=(['\"]?)(\\d+)\\1(?=\\s|>)[^<>]*>", std::regex_constants::icase);
const std::regex tag_remover("</?p\\b[^<>]*>", std::regex_constants::icase);
const std::regex br_replacer("<br\\b[^<>]*>", std::regex_constants::icase);
const std::regex multiline_remover("\n+");
const std::regex space_remover("\\s+");

void setSubtitleCode(double time);
void setSubtitleCodeEmpty(double time);
void createUndoPoint(bool begin = true);
void playerPlayRangeInternal(double begin, double end);
void selectNextLine();

const int nbFunc = 17;
void insertSubtitleCode();
void insertSubtitleCodeEmpty();
void gotoCurrentLine();
void playerJumpTo();
void playerPlayPause();
void playerPlayRange();
void playerClose();
void playerRew();
void playerFF();
void playerRewFrame();
void playerFFFrame();
void addTemplate();
void makeSRT();
void makeASS();
void makeSRTAll();
void makeASSAll();
void useDockedPlayer();

#endif //PLUGINDEFINITION_H
