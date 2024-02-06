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
#include <map>

#include "lb.h"

#define LB_CLIENT_PORT      8080
#define MAX_RECV_SIZE       4096
#define SERVER_PORT         3000
#define CUSTOM_HEADER_SIZE  14

enum serverStatus{
    LB_SERVER_ALIVE,
    LB_SERVER_DEAD,
};

struct lbSocket{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
};

int monitorClientFd(lbSocket, int, int, epoll_event*, std::map<int,int>*);
int monitorServerFd(int, epoll_event*, std::map<int,int>*);
int setupClientListener(lbSocket&, int, int);
int connectToServer(char*, int, int, std::map<int,int>*);
int connectNewClient(lbSocket&, int);

#endif // !NET_H
