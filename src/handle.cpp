#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../lib/net.h"
#include "../lib/handle.h"

using namespace std;

void handleClient(task threadTask, map<int,int>* serverMap){
    
    char buffer[MAX_RECV_SIZE +1];
    ssize_t bytes_received = 0;

    bytes_received = recv(
            threadTask.fd,
            buffer,
            MAX_RECV_SIZE,
            0);

    if(threadTask.type == LB_REQUEST){
        sendToServer(buffer, bytes_received, threadTask.epollFd, serverMap);
    }else{
        sendToClient(buffer, bytes_received);
        close(threadTask.fd);
    }
}

int sendToClient(char buffer[], int length){
    ssize_t bytes_sent;
    int fd = 6;
    printf("%s\n%d\n", buffer, length);
    bytes_sent = send(fd,buffer,length,0);
    return bytes_sent;
}

int sendToServer(char *buffer, int length, int epollFd, map<int,int>* serverMap){

    int fd;
    int rc = 0;
    int firstChunkLength;
    char serverIP[16] = "127.0.0.1";
    char clientFdHeader[15];
    ssize_t bytes_sent;

    fd = connectToServer(serverIP, 3000, epollFd);

    firstChunkLength = 0;
    while(true){
        if(buffer[firstChunkLength] == '\r' &&
                buffer[firstChunkLength+1] == '\n'){
            break;
        }
        firstChunkLength++;
    }

    bytes_sent = send(fd, buffer, firstChunkLength+2, 0);
    if(bytes_sent <= 0){
        return -1;
    }

    addClientFdHeader(clientFdHeader, fd);
    bytes_sent = send(fd, clientFdHeader, CUSTOM_HEADER_SIZE, 0);
    if(bytes_sent <= 0){
        return -1;
    }
    
    bytes_sent = send(
            fd,
            buffer+firstChunkLength+2,
            length-firstChunkLength-2,
            0);
    if(bytes_sent <= 0){
        return -1;
    }
    
    return 0;
}

int addClientFdHeader(char header[], int fd){
    
    header[0] = 'f';
    header[1] = 'd';
    header[2] = ':';
    header[3] = ' ';

    for(int i=11; i>=4; i--){
        header[i] = fd%10 +'0';
        fd /= 10;
    }

    header[12] = '\r';
    header[13] = '\n';
    header[14] = '\0';
    return 0;
}
