#include "enet_platform.h"
#include <enet/enet.h>
#include <SDL_timer.h>
#include <errno.h>
#include <string.h>

enet_uint32 enet_time_get(void) { return SDL_GetTicks(); }

ENetSocket enet_socket_create(ENetSocketType type) {
    return type==ENET_SOCKET_TYPE_DATAGRAM ? lwip_socket(AF_INET,SOCK_DGRAM,0) : -1;
}

static struct sockaddr_in endpoint(const ENetAddress *a) {
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    if (a) { addr.sin_addr.s_addr=a->host; addr.sin_port=lwip_htons(a->port); }
    return addr;
}

int enet_socket_bind(ENetSocket s,const ENetAddress *a) {
    struct sockaddr_in addr=endpoint(a);
    return lwip_bind(s,(struct sockaddr *)&addr,sizeof(addr));
}

void enet_socket_destroy(ENetSocket s) { lwip_close(s); }

int enet_socket_set_option(ENetSocket s,ENetSocketOption opt,int value) {
    if (opt==ENET_SOCKOPT_NONBLOCK) return lwip_ioctl(s,FIONBIO,&value);
    if (opt==ENET_SOCKOPT_BROADCAST) return lwip_setsockopt(s,SOL_SOCKET,SO_BROADCAST,&value,sizeof(value));
    return -1;
}

int enet_socket_send(ENetSocket s,const ENetAddress *a,const ENetBuffer *buffers,size_t count) {
    unsigned char data[ENET_PROTOCOL_MAXIMUM_MTU];
    size_t n=0;
    for (size_t i=0;i<count;++i) {
        if (buffers[i].dataLength>sizeof(data)-n) return -1;
        memcpy(data+n,buffers[i].data,buffers[i].dataLength);
        n+=buffers[i].dataLength;
    }
    struct sockaddr_in addr=endpoint(a);
    int sent=lwip_sendto(s,data,n,0,(struct sockaddr *)&addr,sizeof(addr));
    return sent<0 && errno==EWOULDBLOCK ? 0 : sent;
}

int enet_socket_receive(ENetSocket s,ENetAddress *a,ENetBuffer *buffers,size_t count) {
    if (count!=1) return -1;
    struct sockaddr_in from;
    socklen_t len=sizeof(from);
    int n=lwip_recvfrom(s,buffers->data,buffers->dataLength,MSG_DONTWAIT,(struct sockaddr *)&from,&len);
    if (n<0) return errno==EWOULDBLOCK ? 0 : -1;
    a->host=from.sin_addr.s_addr; a->port=lwip_ntohs(from.sin_port);
    return n;
}

int enet_socket_wait(ENetSocket s,enet_uint32 *condition,enet_uint32 timeout) {
    fd_set read;
    FD_ZERO(&read); FD_SET(s,&read);
    struct timeval tv={timeout/1000,(timeout%1000)*1000};
    int n=lwip_select(s+1,&read,NULL,NULL,&tv);
    *condition=n>0 ? ENET_SOCKET_WAIT_RECEIVE : 0;
    return n<0 ? -1 : 0;
}

// Socket setup is owned by the title so offline duels never wait for DHCP.
int enet_initialize(void) { return 0; }
void enet_deinitialize(void) {}
