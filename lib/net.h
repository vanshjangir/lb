#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <string>

#define LB_CLIENT_PORT      8080
#define MAX_RECV_SIZE       4096
#define SERVER_PORT         3000
#define CUSTOM_HEADER_SIZE  14

enum taskType{
    LB_REQUEST,
    LB_RESPONSE,
};

enum serverStatus{
    LB_SERVER_ALIVE,
    LB_SERVER_DEAD,
};

struct lbSocket{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
};

struct task{
    int fd;
    int epollFd;
    taskType type;
};

void threadExec(int);
int monitorClientFd(lbSocket, int, epoll_event*);
int monitorServerFd(int, epoll_event*);
int setupClientListener(lbSocket&, int, int);
int connectToServer(char*, int, int);

#endif // !NET_H
