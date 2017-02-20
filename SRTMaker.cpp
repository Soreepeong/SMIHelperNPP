#include "PluginDefinition.h"
#include "SRTMaker.h"
#include <algorithm> 
#include <functional> 
#include <cctype>
#include <cwctype>
#include <locale>
#include "entities.h"

inline std::string trim(const std::string &s)
{
	auto  wsfront = std::find_if_not(s.begin(), s.end(), [](int c) {return std::isspace((unsigned char)c); });
	return std::string(wsfront, std::find_if_not(s.rbegin(), std::string::const_reverse_iterator(wsfront), [](int c) {return std::isspace((unsigned char)c); }).base());
}
std::string *convertSMItoSRT(char *subject) {
	std::cregex_iterator next(subject, &subject[strlen(subject)], syncmatcher);
	std::cregex_iterator end;
	std::string *result = new std::string();
	int lastTime = 0, lastPos = 0, index = 1;
	char timecode[512];
	while (next != end) {
		std::cmatch match = *next;
		int time = atoi(match.str(2).c_str());
		if (lastPos != 0) {
			std::string linedata(subject, lastPos, match.position() - lastPos);
			linedata.erase(linedata.begin(), std::find_if(linedata.begin(), linedata.end(), std::not1(std::ptr_fun(std::iswspace))));
			linedata.erase(std::find_if(linedata.rbegin(), linedata.rend(), std::not1(std::ptr_fun(std::iswspace))).base(), linedata.end());
			linedata = std::regex_replace(linedata, tag_remover, "");
			linedata = std::regex_replace(linedata, br_replacer, "\r\n");
			linedata.resize(decode_html_entities_utf8(&linedata[0], NULL));
			std::replace(linedata.begin(), linedata.end(), '\r', '\n');
			linedata = trim(linedata);
			linedata = std::regex_replace(linedata, multiline_remover, "\r\n");
			if (linedata.empty()) {
				lastPos = 0;
			}
			else {
				snprintf(timecode, sizeof(timecode), "%d\r\n%02d:%02d:%02d,%03d --> %02d:%02d:%02d,%03d\r\n", index++
					, lastTime / 3600000, (lastTime / 60000) % 60, (lastTime / 1000) % 60, lastTime % 1000
					, time / 3600000, (time / 60000) % 60, (time / 1000) % 60, time % 1000);
				result->append(timecode);
				result->append(linedata);
				result->append("\r\n\r\n");
			}
		}
		lastPos = match.position() + match.length();
		lastTime = time;
		next++;
	}
	return result;
}
std::string *convertSMItoASS(char *subject) {
	std::cregex_iterator next(subject, &subject[strlen(subject)], syncmatcher);
	std::cregex_iterator end;
	std::string *result = new std::string("[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n");
	int lastTime = 0, lastPos = 0, index = 1;
	char timecode[512];
	while (next != end) {
		std::cmatch match = *next;
		int time = atoi(match.str(2).c_str());
		if (lastPos != 0) {
			std::string linedata(subject, lastPos, match.position() - lastPos);
			linedata.erase(linedata.begin(), std::find_if(linedata.begin(), linedata.end(), std::not1(std::ptr_fun(std::iswspace))));
			linedata.erase(std::find_if(linedata.rbegin(), linedata.rend(), std::not1(std::ptr_fun(std::iswspace))).base(), linedata.end());
			linedata = std::regex_replace(linedata, tag_remover, "");
			linedata = std::regex_replace(linedata, br_replacer, "\r\n");
			linedata.resize(decode_html_entities_utf8(&linedata[0], NULL));
			std::replace(linedata.begin(), linedata.end(), '\r', '\n');
			linedata = trim(linedata);
			linedata = std::regex_replace(linedata, multiline_remover, "\\N");
			if (linedata.empty()) {
				lastPos = 0;
			}
			else {
				snprintf(timecode, sizeof(timecode), "Dialogue: 0,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,Default,,0,0,0,,"
					, lastTime / 3600000, (lastTime / 60000) % 60, (lastTime / 1000) % 60, (lastTime % 1000)/10
					, time / 3600000, (time / 60000) % 60, (time / 1000) % 60, (time % 1000)/10);
				result->append(timecode);
				result->append(linedata);
				result->append("\r\n");
			}
		}
		lastPos = match.position() + match.length();
		lastTime = time;
		next++;
	}
	return result;
}