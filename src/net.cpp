#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <string.h>
#include <fcntl.h>

#include "../lib/lb.h"
#include "../lib/net.h"

using namespace std;

int connectNewClient(lbSocket &clientSocket, int clientEpollFd){
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
            cerr << "fnctl error";
            return -1;
        }

        if(epoll_ctl(clientEpollFd, EPOLL_CTL_ADD, 
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
    int port;
    epoll_event epollEvent;

    int sIndex = pPool->nextServer(ip, port);

    server.addrlen = sizeof(server.addr);
    server.addr.sin_family = AF_INET;
    server.addr.sin_addr.s_addr = inet_addr(ip);
    server.addr.sin_port = htons(port);

    server.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server.fd == -1){
        close(server.fd);
        return -1;
    }

    pPool->setTime(sIndex, server.fd);

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
        lbSocket lbClientSocket,
        int clientEpollFd,
        epoll_event clientEventArray[])
{

    while(true){
        
        if(exitThread)
            break;
        
        int numEvents = epoll_wait(clientEpollFd, clientEventArray, 1000, -1);
        if(numEvents == -1){
            cout << "epoll wait error monitor client\n";
            return -1;
        }

        for(int i=0; i<numEvents; i++){
            
            if(clientEventArray[i].data.fd == lbClientSocket.fd){
                connectNewClient(lbClientSocket, clientEpollFd);
            }
            else if(clientEventArray[i].events & EPOLLIN){

                task queueTask;
                queueTask.type = LB_REQUEST;
                queueTask.fd = clientEventArray[i].data.fd;
                {
                    unique_lock<mutex> lock(threadMutex);
                    taskQueue.push(queueTask);
                }

                threadCondition.notify_one();
            }
            else if(clientEventArray[i].events & EPOLLRDHUP){
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


int setupClientListener(lbSocket &lbClientSocket, int port, int clientEpollFd){

    int flags;
    int reuse = 1;
    epoll_event epollEvent;
    lbClientSocket.addr.sin_family = AF_INET;
    lbClientSocket.addr.sin_port = htons(port);
    lbClientSocket.addr.sin_addr.s_addr = INADDR_ANY;
    lbClientSocket.addrlen = sizeof(lbClientSocket.addr);
    
    lbClientSocket.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(lbClientSocket.fd == -1){
        cerr << "error creating socket lbClientSocket";
        return -1;
    }
    
    if(setsockopt(lbClientSocket.fd, SOL_SOCKET,
                SO_REUSEPORT, &reuse, sizeof(reuse))<0)
    {
        printf("setting SO_REUSEPORT failed\n");
        exit(-1);
    }

    flags = fcntl(lbClientSocket.fd, F_GETFL, 0);
    flags |= O_NONBLOCK;

    if(bind(lbClientSocket.fd,
            reinterpret_cast<struct sockaddr*>(&lbClientSocket.addr),
            lbClientSocket.addrlen) != 0){
        cerr << "bind error lbClientSocket" << endl;
        close(lbClientSocket.fd);
        return -1;
    }

    if(listen(lbClientSocket.fd, 10) != 0){
        cerr << "listen error lbClientSocket" << endl;
        close(lbClientSocket.fd);
        return -1;
    }
    
    if(fcntl(lbClientSocket.fd, F_SETFL, flags) == -1){
        cerr << "fnctl error\n";
        return -1;
    }
    
    epollEvent.events = EPOLLIN;
    epollEvent.data.fd = lbClientSocket.fd;

    if(epoll_ctl(clientEpollFd, EPOLL_CTL_ADD, lbClientSocket.fd, &epollEvent) == -1){
        cerr << "error_ctl error lbClientSocket";
        return -1;
    }
    return 0;
}

void dsr(){

    lbSocket rawSocket;
    char buffer[RAW_BUFFER_SIZE];
    struct sockaddr_in destAddr;

    rawSocket.fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (rawSocket.fd < 0) {
        perror("Failed to create socket");
        return;
    }

    while(true){
        struct iphdr *ip_header;
        struct tcphdr *tcp_header;
        struct iphdr new_ip_header;
        struct in_addr vIP;
        vIP.s_addr = inet_addr(VIP);

        int data_size = recvfrom(
                rawSocket.fd, buffer,
                RAW_BUFFER_SIZE,
                0,
                (struct sockaddr*)&rawSocket.addr,
                &rawSocket.addrlen);

        if (data_size < 0) {
            perror("Failed to receive packet");
            close(rawSocket.fd);
            return;
        }

        ip_header = (struct iphdr *)buffer;
        tcp_header = (struct tcphdr *)(buffer + ip_header->ihl * 4);

        if(memcmp(&vIP, &(ip_header->daddr), sizeof(struct in_addr)) != 0){
            continue;
        }

        printf("Packet received from: %s\n", inet_ntoa(rawSocket.addr.sin_addr));
        printf("Source IP: %s\n", inet_ntoa(*(struct in_addr *)&ip_header->saddr));
        printf("Source Port: %u\n", ntohs(tcp_header->source));
        printf("Destination IP: %s\n", inet_ntoa(*(struct in_addr *)&ip_header->daddr));
        printf("Destination Port: %u\n", ntohs(tcp_header->dest));
        printf("Packet Length: %d\n\n", data_size);
        fflush(stdout);

        memset(&new_ip_header, 0, sizeof(new_ip_header));

        new_ip_header.ihl = 5;
        new_ip_header.version = 4;
        new_ip_header.tot_len = htons(data_size + sizeof(struct iphdr));
        new_ip_header.id = htons(54321);
        new_ip_header.ttl = 64;
        new_ip_header.protocol = IPPROTO_IPIP;
        new_ip_header.saddr = ip_header->saddr;
        new_ip_header.daddr = inet_addr("");

        memmove(buffer + sizeof(struct iphdr), buffer, data_size);
        memcpy(buffer, &new_ip_header, sizeof(struct iphdr));
        memset(&destAddr, 0, sizeof(destAddr));

        destAddr.sin_family = AF_INET;
        destAddr.sin_addr.s_addr = new_ip_header.daddr;

        sendto(rawSocket.fd, buffer, data_size + sizeof(struct iphdr), 0,
               (struct sockaddr *)&destAddr, sizeof(destAddr));

        printf("packet sent\n");
    }
}
