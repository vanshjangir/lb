#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define LB_CLIENT_PORT      8080
#define MAX_RECV_SIZE       4096
#define SERVER_PORT         3000
#define CUSTOM_HEADER_SIZE  14

enum taskType{
    SEND,
    RECEIVE,
};

enum serverStatus{
    ALIVE,
    DEAD,
};

struct lbSocket{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
};

struct task{
    int fd;
    taskType type;
};

void threadExec();
int monitorFd(lbSocket, int, epoll_event*);
int setupSocket(lbSocket&, int, int);


#endif // !NET_H
