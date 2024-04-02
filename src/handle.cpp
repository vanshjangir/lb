#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../lib/net.h"
#include "../lib/handle.h"

using namespace std;

void handleTask(task qtask, ServerPool *pPool){
    
    char buffer[MAX_RECV_SIZE +1];
    ssize_t bytes_received = 0;

    bytes_received = recv(
            qtask.fd,
            buffer,
            MAX_RECV_SIZE,
            0);

    if(qtask.type == LB_REQUEST){
        sendToServer(buffer, bytes_received, qtask, pPool);
    }
    else if(qtask.type == LB_RESPONSE){
        sendToClient(buffer, bytes_received);
        close(qtask.fd);
    }
}

int sendToClient(char buffer[], int buflen){
    ssize_t bytes_sent;
    int fd = getClientResponseFd(buffer, buflen);
    bytes_sent = send(fd,buffer,buflen,0);
    close(fd);
    return bytes_sent;
}

int getClientResponseFd(char buffer[], int buflen){
    
    int fd = 0;
    char *loc = NULL;
    for(int i=0; i<buflen-2; i++){
        if(buffer[i] == 'f' && buffer[i+1] == 'd' && buffer[i+2] == ':'){
            loc = buffer+(i+4);
            break;
        }
    }

    if(loc == NULL){
        return -1;
    }

    for(int i=0; i<8; i++){
        fd += fd*10 +(loc[i] - '0');
    }

    return fd;
}

int sendToServer(char *buffer, int buflen, task& clientTask, ServerPool *pPool){

    int fd;
    int firstChunkLength;
    char clientFdHeader[15];
    ssize_t bytes_sent;

    fd = connectToServer(pPool);

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

    addClientFdHeader(clientFdHeader, clientTask.fd);
    bytes_sent = send(fd, clientFdHeader, CUSTOM_HEADER_SIZE, 0);
    if(bytes_sent <= 0){
        return -1;
    }
    
    bytes_sent = send(
            fd,
            buffer+firstChunkLength+2,
            buflen-firstChunkLength-2,
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
