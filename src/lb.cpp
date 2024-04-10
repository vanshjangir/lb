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

std::atomic<bool> exitThread(false);
std::queue<task> taskQueue;
std::mutex threadMutex;
std::condition_variable threadCondition;

bool _IS_RUNNING = false;
bool _DSR_ENABLE = false;
int _NUM_THREADS = 4;

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
    for(i=0; i<(int)mTable[mCurIndex].ip.size(); i++){
        serverIP[i] = mTable[mCurIndex].ip[i];
    }
    serverIP[i] = '\0';
    port = mTable[mCurIndex].port;
    mCurWeight--;
    return mCurIndex;
}

void ServerPool::listServer(){
    cout << "\nIP Address\tPort\tWeight\tLatency(microseconds)" << endl;
    for(int i=0; i<(int)mTable.size(); i++){
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
        case 't':
            _NUM_THREADS = stoi(arg+2);
        default:
            goto out;
    }
    return 0;

out:
    printf("unexpected argument %s\n", arg);
    return -1;
}

void threadWorker(ServerPool *pPool){

    if(_DSR_ENABLE){
        dsr();
        return;
    }
    
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

int runLB(
        ServerPool *pPool,
        thread *pServerThread,
        thread *pClientThread,
        thread workerThreads[])
{
    lbSocket lbClientSocket;
    epoll_event *clientEventArray;

    const int clientEpollFd = epoll_create1(0);
    if(clientEpollFd == -1){
        cerr << "error creating epoll";
        return -1;
    }

    pPool->epollFd = epoll_create1(0);
    if(pPool->epollFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    pPool->eventArray = new epoll_event[MAX_SERVER_LIMIT];
    clientEventArray = new epoll_event[MAX_CLIENT_LIMIT];

    setupClientListener(lbClientSocket, LB_CLIENT_PORT, clientEpollFd);

    *(pClientThread) = thread(
            monitorClientFd,
            lbClientSocket,
            clientEpollFd,
            clientEventArray);

    *(pServerThread) = thread(monitorServerFd, pPool);
    
    for(int i=0; i<_NUM_THREADS; i++){
        workerThreads[i] = thread(threadWorker, pPool);
    }
    
    _IS_RUNNING = true;
    return 0;
}

int runDSR(ServerPool *pPool, thread workerThreads[]){
    
    for(int i=0; i<_NUM_THREADS; i++){
        workerThreads[i] = thread(threadWorker, pPool);
        workerThreads[i].detach();
    }
        
    _IS_RUNNING = true;
    return 0;
}

void lbExit(
        thread *serverThread = NULL,
        thread *clientThread = NULL,
        thread workerThreads[] = NULL)
{
    
    if(!_IS_RUNNING)
        goto out;

    if(_DSR_ENABLE){
        goto out;
    }

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
    
    for(int i=0; i<4; i++)
        workerThreads[i].join();

out:
    cout << "Exiting...\n";
    exit(0);
}

void cli(
        ServerPool *pPool,
        thread *pClientThread,
        thread *pServerThread,
        thread workerThreads[])
{
    string rawInput;
    vector<string> inputArgs;

    cout << "\nlb[]::";
    getline(cin,rawInput);
    
    parseCliInput(rawInput, inputArgs);

    if(inputArgs[0] == "exit"){
        lbExit(pServerThread, pClientThread, workerThreads);
    }
    else if(inputArgs[0] == "run"){

        int rc;
        if(!pPool->checkHealth()){
            cout << "zero servers found\n";
            lbExit(pServerThread, pClientThread, workerThreads);
        }

        if(_DSR_ENABLE)
            rc = runDSR(pPool, workerThreads);
        else
            rc = runLB(pPool, pServerThread, pClientThread, workerThreads);

        if(rc != 0)
            cout << "error running load balancer\n";
    }
    else if(inputArgs[0] == "list"){
        pPool->listServer();
    }
    else if(inputArgs[0] == "add"){
        if(inputArgs.size() == 4){
            pPool->addServer(
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

int main(int argc, char **argv){

    if(argc>1){
        for(int i=1; i<argc; i++){
            if(parseCmdArgs(argv[i]) != 0)
                lbExit();
        }
    }
    
    ServerPool pool;
    thread workerThreads[4];

    if(_DSR_ENABLE){
        while(true){
            cli(&pool, NULL, NULL, workerThreads);
        }
        return 0;
    }

    thread serverThread;
    thread clientThread;

    while(true){
        cli(&pool, &clientThread, &serverThread, workerThreads);
    }
    return 0;
}
