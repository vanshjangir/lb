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

    printf("%s\n%ld\n", buffer, bytes_received);

    sendToServer(buffer, bytes_received, serverMap);
}

int sendToServer(char *buffer, int length, map<int,int>* serverMap){

    int rc = 0;
    int firstChunkLength;
    char serverIP[16] = "127.0.0.1";
    char clientFdHeader[15];
    ssize_t bytes_sent;
    lbSocket serverSocket;
    
    //getNextServerIP(serverIP);

    serverSocket.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket.fd == -1){
        printf("socket errors\n");
        return -1;
    }
    serverSocket.addrlen = sizeof(serverSocket.addr);
    serverSocket.addr.sin_addr.s_addr = inet_addr(serverIP);
    serverSocket.addr.sin_port = htons(SERVER_PORT);
    serverSocket.addr.sin_family = AF_INET;

    while(true){
        rc = connect(
                serverSocket.fd,
                (struct sockaddr*)&serverSocket.addr,
                serverSocket.addrlen
                );
        if(rc == -1){
            sleep(1);
        }else{
            (*serverMap)[serverSocket.fd] = ALIVE;
            printf("alive\n");
            break;
        }
    }

    firstChunkLength = 0;
    while(true){
        if(buffer[firstChunkLength] == '\r' &&
                buffer[firstChunkLength+1] == '\n'){
            break;
        }
        firstChunkLength++;
    }

    bytes_sent = send(serverSocket.fd, buffer, firstChunkLength+2, 0);
    if(bytes_sent <= 0){
        close(serverSocket.fd);
        return -1;
    }

    addClientFdHeader(clientFdHeader, serverSocket.fd);
    bytes_sent = send(serverSocket.fd, clientFdHeader, CUSTOM_HEADER_SIZE, 0);
    
    bytes_sent = send(
            serverSocket.fd,
            buffer+firstChunkLength+2,
            length-firstChunkLength-2,
            0);
    if(bytes_sent <= 0){
        close(serverSocket.fd);
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
