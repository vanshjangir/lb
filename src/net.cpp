#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "../lib/net.h"

using namespace std;

queue<task> taskQueue;
mutex threadMutex;
condition_variable threadCondition;

void handle_client(task threadTask){
    char buf[3];
    cout << "received on " << threadTask.fd << "\n";
    recv(threadTask.fd, buf, 2, 0);
    buf[2] = '\0';
    cout << buf << endl;
}

void threadExec(){
    
    while(true){
        task threadTask;
        {
            unique_lock<mutex> lock(threadMutex);
            threadCondition.wait(lock, [&]{ return !taskQueue.empty(); });
            threadTask = taskQueue.front();
            taskQueue.pop();
        }
        // fucntion for fd
        handle_client(threadTask);
    }
}

int monitorFd(lbSocket Conn, int epollFd, epoll_event epollFdArray[]){

    while(true){

        int numEvents = epoll_wait(epollFd, epollFdArray, 1000, -1);

        if(numEvents == -1){
            cout << "epoll wait error\n";
            return -1;
        }

        for(int i=0; i<numEvents; i++){

            if(epollFdArray[i].data.fd == Conn.fd){
                lbSocket clientConn;
                clientConn.fd = accept(Conn.fd,
                            reinterpret_cast<sockaddr*>(&Conn.addr),
                            &Conn.addrlen);

                if(clientConn.fd != -1){

                    int flags = fcntl(clientConn.fd, F_GETFL, 0);
                    epoll_event epollEvent;
                    flags |= O_NONBLOCK;
                    epollEvent.data.fd = clientConn.fd;
                    epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;

                    if(fcntl(clientConn.fd, F_SETFL, flags) == -1){
                        cerr << "fnctl error";
                        return -1;
                    }

                    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, 
                                clientConn.fd, &epollEvent) == -1){
                        cout << "epoll_ctl client error\n";
                        close(clientConn.fd);
                    }else{
                        cout << "new connection\n";
                    }
                } 
            }
            else if(epollFdArray[i].events & EPOLLIN){
                task threadTask;
                threadTask.fd = epollFdArray[i].data.fd;
                threadTask.type = RECEIVE;
                {
                    unique_lock<mutex> lock(threadMutex);
                    taskQueue.push(threadTask);
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

int setupSocket(lbSocket &socketInfo, int port, int epollFd){

    epoll_event epollEvent;
    int flags;
    socketInfo.addr.sin_family = AF_INET;
    socketInfo.addr.sin_port = htons(port);
    socketInfo.addr.sin_addr.s_addr = INADDR_ANY;
    socketInfo.addrlen = sizeof(socketInfo.addr);
    
    socketInfo.fd = socket(AF_INET, SOCK_STREAM, 0);

    if(socketInfo.fd == -1){
        cerr << "error creating socket socketInfo";
        return -1;
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

    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, socketInfo.fd, &epollEvent) == -1){
        cerr << "error_ctl error socketInfo";
        return -1;
    }
    return 0;
}
