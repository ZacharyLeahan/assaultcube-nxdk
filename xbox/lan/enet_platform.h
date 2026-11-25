#pragma once
/* Provide ENet's platform types from nxdk/lwIP instead of POSIX or Winsock. */
#include <lwip/sockets.h>
#define __ENET_UNIX_H__
#define __ENET_WIN32_H__
#define ENET_CALLBACK
#define ENET_API extern
#define ENET_SOCKET_NULL (-1)
#define ENET_HOST_TO_NET_16 lwip_htons
#define ENET_NET_TO_HOST_16 lwip_ntohs
#define ENET_HOST_TO_NET_32 lwip_htonl
#define ENET_NET_TO_HOST_32 lwip_ntohl
typedef int ENetSocket;
typedef fd_set ENetSocketSet;
typedef struct { void *data; size_t dataLength; } ENetBuffer;
