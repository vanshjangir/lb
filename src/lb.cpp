#include <iostream>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <atomic>

#include "../lib/net.h"
#include "../lib/lb.h"
#include "../lib/handle.h"

#define BUFFER_SIZE 65536

std::atomic<bool> exitThread(false);
std::queue<task> taskQueue;
std::mutex threadMutex;
std::condition_variable threadCondition;

bool _IS_RUNNING = false;
bool _DSR_ENABLE = false;

using namespace std;

ServerProp::ServerProp(string ip, int port, int weight){
    this->ip = ip;
    this->port = port;
    this->weight = weight;
    this->avgLatency = 0;
    this->nreq = 0;
}

ServerPool::ServerPool(){
    // for testing
    mTable.push_back({"127.0.0.1", 3000, 1});
    // for testing 


    mCurIndex = 0;
    mCurWeight = mTable[0].weight;
}

bool ServerPool::checkHealth(){
    if(mTable.size() <= 0)
        return false;
    else
        return true;
}

int ServerPool::nextServer(char *serverIP, int &port){
    if(mCurWeight <= 0){
        mCurIndex = (mCurIndex+1)%(mTable.size());
        mCurWeight = mTable[mCurIndex].weight;
    }

    int i;
    for(i=0; i<mTable[mCurIndex].ip.size(); i++){
        serverIP[i] = mTable[mCurIndex].ip[i];
    }
    serverIP[i] = '\0';
    port = mTable[mCurIndex].port;
    mCurWeight--;
    return mCurIndex;
}

void ServerPool::listServer(){
    cout << "\nIP Address\tPort\tWeight\tLatency(microseconds)" << endl;
    for(int i=0; i<mTable.size(); i++){
        cout << mTable[i].ip << "\t";
        cout << mTable[i].port << "\t";
        cout << mTable[i].weight << "\t";
        cout << mTable[i].avgLatency << endl;
    }
}

void ServerPool::addServer(const char *serverIP, int port, int weight){

    /* testing the server before pushing,
     * not implemented yet
     */

    string s(serverIP);
    ServerProp newServer(s, port, weight);
    mTable.push_back(newServer);
}

void ServerPool::setTime(int index, int fd){
    mTable[index].curtime = chrono::high_resolution_clock::now();
    mTable[index].nreq++;
}

void ServerPool::setLatency(int fd){
    auto end = chrono::high_resolution_clock::now();
    int index = mFdToServer[fd];
    if(mTable[index].avgLatency == 0){
        mTable[index].avgLatency = chrono::duration_cast<std::chrono::microseconds>
            (end - mTable[index].curtime).count();
    }
    else{
        int nreq = mTable[index].nreq;
        int avgLatency = mTable[index].avgLatency;
        mTable[index].avgLatency = (
                avgLatency*(nreq-1) +
                chrono::duration_cast<std::chrono::microseconds>
                (end - mTable[index].curtime).count()
                )/(nreq);
    }
}

/* Parse the input char array into a vector of strings */
void parseCliInput(string rawInput, vector<string> &inputArgs){

    for(int i=0; i<(int)rawInput.size(); i++){
        string temp = "";

        while(true){
            if(rawInput[i] == ' ' || i == (int)rawInput.size()){
                break;
            }
            temp += rawInput[i];
            i++;
        }
        inputArgs.push_back(temp);
    }
}

int parseCmdArgs(char *arg){

    if(arg[0] != '-')
        goto out;

    switch(arg[1]){
        case '-':
            if(strncmp(arg+2, "DSR", 3) == 0){
                _DSR_ENABLE = true;
                return 0;
            }
            else
                goto out;
        default:
            goto out;
    }
    return 0;

out:
    printf("unexpected argument %s\n", arg);
    return -1;
}

void threadWorker(ServerPool *pPool){
    
    while(!exitThread){
        task qtask;
        {
            unique_lock<mutex> lock(threadMutex);
            threadCondition.wait(lock, [&]{ return !taskQueue.empty(); });
            if(exitThread){
                break;
            }

            qtask = taskQueue.front();
            taskQueue.pop();
        }
        handleTask(qtask, pPool);
    }
}

void threadDsr(lbSocket *pRawSocket){

    char buffer[BUFFER_SIZE];
    struct sockaddr_in destAddr;

    while (1) {

        struct iphdr *ip_header;
        struct tcphdr *tcp_header;
        struct iphdr new_ip_header;

        int data_size = recvfrom(
                pRawSocket->fd, buffer,
                BUFFER_SIZE,
                0,
                (struct sockaddr*)&pRawSocket->addr,
                &pRawSocket->addrlen);

        if (data_size < 0) {
            perror("Failed to receive packet");
            close(pRawSocket->fd);
            return;
        }

        ip_header = (struct iphdr *)buffer;
        tcp_header = (struct tcphdr *)(buffer + ip_header->ihl * 4);

        if(ip_header->protocol != IPPROTO_TCP ||
                memcmp(
                    &(pRawSocket->addr.sin_addr),
                    &(ip_header->daddr),
                    sizeof(struct in_addr)
                    ) == 0){
            continue;
        }

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

        sendto(pRawSocket->fd, buffer, data_size + sizeof(struct iphdr), 0,
               (struct sockaddr *)&destAddr, sizeof(destAddr));

        printf("packet sent\n");
    }
}

int runLB(
        ServerPool &pool,
        thread &serverThread,
        thread &clientThread,
        thread workerThreads[])
{
    lbSocket lbClientSocket;
    epoll_event *clientEventArray;

    const int clientEpollFd = epoll_create1(0);
    if(clientEpollFd == -1){
        cerr << "error creating epoll";
        return -1;
    }

    pool.epollFd = epoll_create1(0);
    if(pool.epollFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    pool.eventArray = new epoll_event[MAX_SERVER_LIMIT];
    clientEventArray = new epoll_event[MAX_CLIENT_LIMIT];

    setupClientListener(lbClientSocket, LB_CLIENT_PORT, clientEpollFd);

    clientThread = thread(
            monitorClientFd,
            lbClientSocket,
            clientEpollFd,
            clientEventArray);

    serverThread = thread(monitorServerFd, &pool);
    
    workerThreads[0] = thread(threadWorker, &pool);
    workerThreads[1] = thread(threadWorker, &pool);
    workerThreads[2] = thread(threadWorker, &pool);
    workerThreads[3] = thread(threadWorker, &pool);

    _IS_RUNNING = true;
    return 0;
}

int runDSR(ServerPool &pool, thread &clientThread){

    lbSocket rawSocket;
    rawSocket.fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (rawSocket.fd < 0) {
        perror("Failed to create socket");
        return -1;
    }

    clientThread = thread(threadDsr, &rawSocket);
    return 0;
}

void lbExit(
        thread *serverThread = NULL,
        thread *clientThread = NULL,
        thread workerThreads[] = NULL)
{
    
    if(!_IS_RUNNING || _DSR_ENABLE)
        goto out;

    if(serverThread == NULL ||
            clientThread == NULL ||
            workerThreads == NULL)
        goto out;
    
    exitThread = true;

    {
        task dummy;
        dummy.type = LB_DUMMY;
        unique_lock<mutex> lock(threadMutex);
        taskQueue.push(dummy);
    }

    threadCondition.notify_all();
    (*serverThread).detach();
    (*clientThread).detach();

    for(int i=0; i<4; i++){
        workerThreads[i].join();
    }

out:
    cout << "Exiting...";
    cout << "bye\n";
    exit(0);
}

int main(int argc, char **argv){

    if(argc>1){
        for(int i=1; i<argc; i++){
            if(parseCmdArgs(argv[i]) != 0)
                lbExit();
        }
    }

    ServerPool pool;
    thread serverThread;
    thread clientThread;
    thread workerThreads[4];

    while(true){
        
        string rawInput;
        vector<string> inputArgs;

        cout << "\nlb[]::";
        getline(cin,rawInput);
        
        parseCliInput(rawInput, inputArgs);

        if(inputArgs[0] == "exit"){
            lbExit(&serverThread, &clientThread, workerThreads);
        }
        else if(inputArgs[0] == "run"){
            int rc;

            if(!pool.checkHealth()){
                cout << "zero servers found\n";
                lbExit(&serverThread, &clientThread, workerThreads);
            }

            if(_DSR_ENABLE){
                rc = runDSR(pool, clientThread);
            }
            else{
                rc = runLB(pool, serverThread, clientThread, workerThreads);
            }

            if(rc == 0){
                cout << "listening on port 8080...\n";
            }
            else{
                cout << "error running load balancer\n";
            }
        }
        else if(inputArgs[0] == "list"){
            pool.listServer();            
        }
        else if(inputArgs[0] == "add"){
            if(inputArgs.size() == 4){
                pool.addServer(
                        inputArgs[1].c_str(),
                        stoi(inputArgs[2]),
                        stoi(inputArgs[3])
                        );
            }
            else{
                cout << "incorrect arguments\n";
            }
        }
        else{
            cout << "invalid command" << endl;
        }
    }
}
