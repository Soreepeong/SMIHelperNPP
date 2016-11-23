#include "PluginDefinition.h"
#include "menuCmdID.h"
#include "MpcHcRemote.h"
#include "SRTMaker.h"

std::regex positionMatcher("<p id=\"position\">([0-9]+)</p>");

std::string* getRequest(char *request) {
	struct sockaddr_in localhost;
	memset(&localhost, 0, sizeof(struct sockaddr_in));
	std::string *s = new std::string();
	char buf[65536];
	DWORD port=0, len=4;

	if (NULL != RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\MPC-HC\\MPC-HC\\Settings"), TEXT("WebServerPort"), 0x00010000 | RRF_RT_REG_DWORD, NULL, &port, &len) || port == 0) {
		return NULL;
	}

	localhost.sin_family = AF_INET;
	localhost.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	localhost.sin_port = htons(port);

	SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ConnectSocket == INVALID_SOCKET) {
		return NULL;
	}
	if (connect(ConnectSocket, (struct sockaddr*)&localhost, sizeof(localhost)) == SOCKET_ERROR) {
		closesocket(ConnectSocket);
		ConnectSocket = INVALID_SOCKET;
	}
	if (ConnectSocket == INVALID_SOCKET)
		return NULL;
	if (SOCKET_ERROR == send(ConnectSocket, request, strlen(request), 0)) {
		closesocket(ConnectSocket);
		return NULL;
	}
	if (shutdown(ConnectSocket, SD_SEND) == SOCKET_ERROR) {
		closesocket(ConnectSocket);
		return NULL;
	}


	// Receive data until the server closes the connection
	int got;
	do {
		got = recv(ConnectSocket, buf, sizeof(buf), 0);
		if (got > 0)
			s->append(buf, got);
	} while (got > 0);
	closesocket(ConnectSocket);
	return s;
}

int getMpcHcTime() {
	std::string *s = getRequest("GET /variables.html HTTP/1.1\r\n\r\n");
	if (s == NULL)
		return -1;
	std::smatch m;
	int pos = -1;
	if (std::regex_search(*s, m, positionMatcher)) {
		pos = atoi(m[1].str().c_str());
	}
	delete s;
	return pos;
}

int sendMpcHcCommand(int cmd) {
	char req[65536], req2[1024];
	snprintf(req2, sizeof(req2), "wm_command=%d", cmd);
	snprintf(req, sizeof(req), "POST /command.html HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s", strlen(req2), req2);
	std::string *res = getRequest(req);
	if (res != NULL) {
		delete res;
		return 0;
	}
	return -1;
}

int seekMpcHc(int position) {
	char req[65536], req2[1024];
	snprintf(req2, sizeof(req2), "wm_command=-1&position=%d:%d:%d:%d", position/3600000, (position/60000) % 60, (position/1000) % 60, position % 1000);
	snprintf(req, sizeof(req), "POST /command.html HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s", strlen(req2), req2);
	std::string *res = getRequest(req);
	if (res != NULL) {
		delete res;
		return 0;
	}
	return -1;
}