#pragma once


#ifdef __linux__
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#elif _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Mswsock.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#endif

#ifndef BASESOCKET_DEFINE
#define BASESOCKET_DEFINE
#ifdef __linux__
using BaseSocket = int;
#elif _WIN32
using BaseSocket = SOCKET;
#endif
#endif

#include "NetExportMarco.h"

bool NET_API CloseSocket(BaseSocket);
bool NET_API IsInvaildSocket(BaseSocket);

constexpr BaseSocket Invaild_Socket = 0;
