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

struct lbSocket{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
};
    
int lbServer(lbSocket &clientSocket, int clientEpollFd);

int lbServerSetup(lbSocket &lbClientSocket, int port, int clientEpollFd);

int serverFdLoop(ServerPool *pPool);

int lbClient(int clientFd, ServerPool *pPool);

int clientFdLoop(lbSocket lbClientSocket,
                 int clientEpollFd, epoll_event* clientEventArray);

void dsr(ServerPool *pPool);

#endif // !NET_H
