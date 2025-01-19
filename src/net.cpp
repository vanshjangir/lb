#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cstdlib>

#include "../lib/lb.h"
#include "../lib/net.h"

using namespace std;

int lbServer(lbSocket &clientSocket, int clientEpollFd){
    lbSocket client;
    client.fd = accept(clientSocket.fd,
                reinterpret_cast<sockaddr*>(&clientSocket.addr),
                &clientSocket.addrlen);

    if(client.fd != -1){
        int flags = fcntl(client.fd, F_GETFL, 0);
        epoll_event epollEvent;
        flags |= O_NONBLOCK;
        epollEvent.data.fd = client.fd;
        epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;

        if(fcntl(client.fd, F_SETFL, flags) == -1){
            setLOG("fnctl error");
            return -1;
        }

        if(epoll_ctl(clientEpollFd, EPOLL_CTL_ADD, 
                    client.fd, &epollEvent) == -1){
            setLOG("epoll_ctl client error");
            close(client.fd);
        }else{
            setLOG("new connection at client fd:"+to_string(client.fd));
        }
    }
    return 0;
}

int lbClient(int clientFd, ServerPool *pPool){
    int rc;
    int flags;
    lbSocket server;
    char ip[16];
    int port;
    epoll_event epollEvent;
    ClientHash cHash;

    cHash = fdToClient[clientFd];

    int sIndex = pPool->nextServer(&cHash ,ip, port);

    server.addrlen = sizeof(server.addr);
    server.addr.sin_family = AF_INET;
    server.addr.sin_addr.s_addr = inet_addr(ip);
    server.addr.sin_port = htons(port);

    server.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server.fd == -1){
        close(server.fd);
        return -1;
    }

    pPool->addIndex(server.fd);
    pPool->setTime(sIndex);

    rc = connect(
            server.fd,
            (struct sockaddr*)&server.addr,
            server.addrlen
            );
    if(rc == 0){
        setLOG("connected properly with " + to_string(server.fd));
    }else{
        setLOG("unsuccessful");
        close(server.fd);
        return -1;
    }
    
    flags = fcntl(server.fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    rc = fcntl(server.fd, F_SETFL, flags);
    if(rc == -1){
        close(server.fd);
        return -1;
    }

    epollEvent.data.fd = server.fd;
    epollEvent.events = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;

    rc = epoll_ctl(pPool->epollFd, EPOLL_CTL_ADD, server.fd, &epollEvent);
    if(rc == -1){
        close(server.fd);
        return -1;
    }
    return server.fd;
}

int clientFdLoop(
        lbSocket lbClientSocket,
        int clientEpollFd,
        epoll_event clientEventArray[])
{

    while(true){
        
        if(exitThread)
            break;
        
        int numEvents = epoll_wait(clientEpollFd, clientEventArray, 1000, -1);
        if(numEvents == -1){
            setLOG("epoll wait error monitor client");
            return -1;
        }

        for(int i=0; i<numEvents; i++){
            
            if(clientEventArray[i].data.fd == lbClientSocket.fd){
                lbServer(lbClientSocket, clientEpollFd);
            }
            else if(clientEventArray[i].events & EPOLLIN){

                Task task;
                task.type = LB_REQUEST;
                task.fd = clientEventArray[i].data.fd;
                {
                    unique_lock<mutex> lock(threadMutex);
                    taskQueue.push(task);
                }

                threadCondition.notify_one();
            }
            else if(clientEventArray[i].events & EPOLLRDHUP){
                setLOG("disconnected");
                printLOG("Disconnected from client");
            }
        }
    }
    return 0;
}

int serverFdLoop(ServerPool *pPool){

    while(true){

        if(exitThread)
            break;

        int numEvents = epoll_wait(pPool->epollFd, pPool->eventArray, 1000, -1);
        if(numEvents == -1){
            setLOG("epoll wait error server fd");
            return -1;
        }

        for(int i=0; i<numEvents; i++){
            
            if(pPool->eventArray[i].events & EPOLLIN){
                Task queueTask;
                queueTask.type = LB_RESPONSE;
                queueTask.fd = pPool->eventArray[i].data.fd;
                
                {
                    unique_lock<mutex> lock(threadMutex);
                    taskQueue.push(queueTask);
                }

                threadCondition.notify_one();
            }
            else if(pPool->eventArray[i].events & EPOLLRDHUP){
                setLOG("disconnected");
                printLOG("Disconnected from server");
            }
        }
    }
    return 0;
}


int lbServerSetup(lbSocket &lbClientSocket, int port, int clientEpollFd){

    int flags;
    int reuse = 1;
    epoll_event epollEvent;
    lbClientSocket.addr.sin_family = AF_INET;
    lbClientSocket.addr.sin_port = htons(port);
    lbClientSocket.addr.sin_addr.s_addr = INADDR_ANY;
    lbClientSocket.addrlen = sizeof(lbClientSocket.addr);
    
    lbClientSocket.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(lbClientSocket.fd == -1){
        setLOG("error creating socket lbClientSocket");
        return -1;
    }
    
    if(setsockopt(lbClientSocket.fd, SOL_SOCKET,
                SO_REUSEPORT, &reuse, sizeof(reuse))<0)
    {
        setLOG("setting SO_REUSEPORT failed");
        exit(-1);
    }

    flags = fcntl(lbClientSocket.fd, F_GETFL, 0);
    flags |= O_NONBLOCK;

    if(bind(lbClientSocket.fd,
            reinterpret_cast<struct sockaddr*>(&lbClientSocket.addr),
            lbClientSocket.addrlen) != 0){
        setLOG("bind error lbClientSocket");
        close(lbClientSocket.fd);
        return -1;
    }

    if(listen(lbClientSocket.fd, 10) != 0){
        setLOG("listen error lbClientSocket");
        close(lbClientSocket.fd);
        return -1;
    }
    
    if(fcntl(lbClientSocket.fd, F_SETFL, flags) == -1){
        setLOG("fnctl error");
        return -1;
    }
    
    epollEvent.events = EPOLLIN;
    epollEvent.data.fd = lbClientSocket.fd;

    if(epoll_ctl(clientEpollFd, EPOLL_CTL_ADD, lbClientSocket.fd, &epollEvent) == -1){
        setLOG("error_ctl error lbClientSocket");
        return -1;
    }
    return 0;
}

void dsr(ServerPool *pPool){
}
