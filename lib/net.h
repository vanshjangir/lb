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

/* whether server is connected or not */
enum serverStatus{
    LB_SERVER_ALIVE,
    LB_SERVER_DEAD,
};

/* socket info struct */
struct lbSocket{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
};

/* monitor a socket for events happening with client connection */
int monitorClientFd(lbSocket, int, int, epoll_event*, std::map<int,int>*);

/* monitor a socket for events happening with server connection */
int monitorServerFd(int, epoll_event*, std::map<int,int>*);
    
/* setup a socket to listen for client connections */
int setupClientListener(lbSocket&, int, int);

/* connect a server to the load balancer */
int connectToServer(char*, int, int, std::map<int,int>*);

/* connect a client to the load balancer */
int connectNewClient(lbSocket&, int);

#endif // !NET_H
