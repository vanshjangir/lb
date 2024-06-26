#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "lb.h"

#define LB_CLIENT_PORT      8080
#define MAX_RECV_SIZE       4096
#define SERVER_PORT         3000
#define CUSTOM_HEADER_SIZE  14
#define RAW_BUFFER_SIZE     65536

/* socket info struct */
struct lbSocket{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
};

/* monitor a socket for events happening with client connection */
int monitorClientFd(
        lbSocket lbClientSocket,
        int clientEpollFd,
        epoll_event* clientEventArray);

/* monitor a socket for events happening with server connection */
int monitorServerFd(ServerPool *pPool);
    
/* setup a socket to listen for client connections */
int setupClientListener(lbSocket &lbClientSocket, int port, int clientEpollFd);

/* connect a server to the load balancer */
int connectToServer(int clientFd, ServerPool *pPool);

/* connect a client to the load balancer */
int connectNewClient(lbSocket &clientSocket, int clientEpollFd);

/* direct server return model */
void dsr(ServerPool *pPool);

#endif // !NET_H
