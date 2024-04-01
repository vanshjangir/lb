#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "../lib/lb.h"
#include "../lib/net.h"

using namespace std;

int connectNewClient(lbSocket &conn, int epollClientFd){
    lbSocket client;
    client.fd = accept(conn.fd,
                reinterpret_cast<sockaddr*>(&conn.addr),
                &conn.addrlen);

    if(client.fd != -1){
        int flags = fcntl(client.fd, F_GETFL, 0);
        epoll_event epollEvent;
        flags |= O_NONBLOCK;
        epollEvent.data.fd = client.fd;
        epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;

        if(fcntl(client.fd, F_SETFL, flags) == -1){
            cerr << "fnctl error";
            return -1;
        }

        if(epoll_ctl(epollClientFd, EPOLL_CTL_ADD, 
                    client.fd, &epollEvent) == -1){
            cout << "epoll_ctl client error\n";
            close(client.fd);
        }else{
            cout << "new connection at client fd:" << client.fd << "\n";
        }
    }
    return 0;
}

int connectToServer(ServerPool *pPool){
    int rc;
    int flags;
    lbSocket server;
    char ip[16];
    epoll_event epollEvent;

    pPool->nextIP(ip);

    server.addrlen = sizeof(server.addr);
    server.addr.sin_family = AF_INET;
    server.addr.sin_addr.s_addr = inet_addr(ip);
    server.addr.sin_port = htons(3000);

    server.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server.fd == -1){
        close(server.fd);
        return -1;
    }

    rc = connect(
            server.fd,
            (struct sockaddr*)&server.addr,
            server.addrlen
            );
    if(rc == 0){
        cout << "connected properly with " << server.fd << endl;
    }else{
        cout << "unsuccessful\n";
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

int monitorClientFd(
        lbSocket conn,
        int epollClientFd,
        int epollServerFd,
        epoll_event epollFdArray[])
{

    while(true){
        
        if(exitThread)
            break;
        
        int numEvents = epoll_wait(epollClientFd, epollFdArray, 1000, -1);
        if(numEvents == -1){
            cout << "epoll wait error monitor client\n";
            return -1;
        }

        for(int i=0; i<numEvents; i++){
            
            if(epollFdArray[i].data.fd == conn.fd){
                connectNewClient(conn, epollClientFd);
            }
            else if(epollFdArray[i].events & EPOLLIN){

                task queueTask;
                queueTask.type = LB_REQUEST;
                queueTask.fd = epollFdArray[i].data.fd;
                {
                    unique_lock<mutex> lock(threadMutex);
                    taskQueue.push(queueTask);
                }

                threadCondition.notify_one();
            }
            else if(epollFdArray[i].events & EPOLLRDHUP){
                cout << "disconnected\n";
            }
        }
    }
    return 0;
}

int monitorServerFd(ServerPool *pPool){

    while(true){

        if(exitThread)
            break;

        int numEvents = epoll_wait(pPool->epollFd, pPool->eventArray, 1000, -1);
        if(numEvents == -1){
            cout << "epoll wait error server fd\n";
            return -1;
        }

        for(int i=0; i<numEvents; i++){
            
            if(pPool->eventArray[i].events & EPOLLIN){
                task queueTask;
                queueTask.type = LB_RESPONSE;
                queueTask.fd = pPool->eventArray[i].data.fd;
                
                {
                    unique_lock<mutex> lock(threadMutex);
                    taskQueue.push(queueTask);
                }

                threadCondition.notify_one();
            }
            else if(pPool->eventArray[i].events & EPOLLRDHUP){
                cout << "disconnected\n";
            }
        }
    }
    return 0;
}


int setupClientListener(lbSocket &socketInfo, int port, int epollClientFd){

    epoll_event epollEvent;
    int flags;
    int reuse = 1;
    socketInfo.addr.sin_family = AF_INET;
    socketInfo.addr.sin_port = htons(port);
    socketInfo.addr.sin_addr.s_addr = INADDR_ANY;
    socketInfo.addrlen = sizeof(socketInfo.addr);
    
    socketInfo.fd = socket(AF_INET, SOCK_STREAM, 0);

    if(socketInfo.fd == -1){
        cerr << "error creating socket socketInfo";
        return -1;
    }
    
    if(setsockopt(socketInfo.fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse))<0){
        printf("setting SO_REUSEPORT failed\n");
        exit(-1);
    }

    flags = fcntl(socketInfo.fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    

    if(bind(socketInfo.fd,
            reinterpret_cast<struct sockaddr*>(&socketInfo.addr),
            socketInfo.addrlen) != 0){
        cerr << "bind error socketInfo" << endl;
        close(socketInfo.fd);
        return -1;
    }

    if(listen(socketInfo.fd, 10) != 0){
        cerr << "listen error socketInfo" << endl;
        close(socketInfo.fd);
        return -1;
    }
    
    if(fcntl(socketInfo.fd, F_SETFL, flags) == -1){
        cerr << "fnctl error\n";
        return -1;
    }
    
    epollEvent.events = EPOLLIN;
    epollEvent.data.fd = socketInfo.fd;

    if(epoll_ctl(epollClientFd, EPOLL_CTL_ADD, socketInfo.fd, &epollEvent) == -1){
        cerr << "error_ctl error socketInfo";
        return -1;
    }
    return 0;
}
