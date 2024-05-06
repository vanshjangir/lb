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
#include <fstream>

#include "../lib/net.h"
#include "../lib/lb.h"
#include "../lib/handle.h"

std::atomic<bool> exitThread(false);
std::queue<task> taskQueue;
std::mutex threadMutex;
std::condition_variable threadCondition;
std::ofstream logStream;
std::map<int,ClientHash> fdToClient;
std::string _LB_IP = "";

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
    mCurIndex = 0;
}

bool ServerPool::checkHealth(){
    if(mTable.size() <= 0)
        return false;
    else
        return true;
}

int ServerPool::nextServer(ClientHash *pCHash, char *serverIP, int &port){

    if(mClientToServer[*pCHash].first == ""){
        mCurIndex = (mCurIndex+1)%(mTable.size());
        mClientToServer[*pCHash].first = mTable[mCurIndex].ip;
        mClientToServer[*pCHash].second = mTable[mCurIndex].port;
    }
    else{
    }

    int i;
    string ip = mClientToServer[*pCHash].first;

    for(i=0; i<(int)ip.size(); i++){
        serverIP[i] = mTable[mCurIndex].ip[i];
    }
    serverIP[i] = '\0';
    port = mClientToServer[*pCHash].second;
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

void ServerPool::setTime(int index){
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

void ServerPool::getServerData(vector<pair<string,int>> *pServerTable){

    for(int i=0; i<mTable.size(); i++){
        pServerTable->push_back({mTable[i].ip, mTable[i].port});
    }
}

void setLOG(string s){
    logStream << s << endl;
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
            else if(strncmp(arg+2, "LB_IP=", 6) == 0){
                _LB_IP = arg+8;
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
        dsr(pPool);
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
        setLOG("error creating epoll");
        return -1;
    }

    pPool->epollFd = epoll_create1(0);
    if(pPool->epollFd == -1){
        setLOG("error creating epoll");
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

int runDSR(ServerPool *pPool){

    dsr(pPool);
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
        system("iptables --table nat --flush");
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
    cout << "Load Balancer down\n";
}

void cli(
        ServerPool *pPool,
        thread *pClientThread,
        thread *pServerThread,
        thread workerThreads[])
{
    string rawInput;
    vector<string> inputArgs;

    printf("lb[%s/%s]::", _IS_RUNNING? "up": "down", _DSR_ENABLE? "dsr": "normal");
    getline(cin,rawInput);

    if(rawInput == "")
        return;
    
    parseCliInput(rawInput, inputArgs);

    if(inputArgs[0] == "exit"){
        lbExit(pServerThread, pClientThread, workerThreads);
        cout << "Exiting...\n";
        exit(0);
    }
    else if(inputArgs[0] == "down"){
        lbExit(pServerThread, pClientThread, workerThreads);
    }
    else if(inputArgs[0] == "up"){

        int rc;
        if(!pPool->checkHealth()){
            cout << "zero servers found\n";
            lbExit(pServerThread, pClientThread, workerThreads);
            return;
        }

        //if(_DSR_ENABLE)
        //    rc = runDSR(pPool);
        //else
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

    if(_LB_IP == ""){
        printf("Load balancer IP not given in cmd\n");
        return -1;
    }
    
    ServerPool pool;
    logStream = ofstream("debug.log");

    if(_DSR_ENABLE){
        while(true){
            cli(&pool, NULL, NULL, NULL);
        }
        return 0;
    }

    thread workerThreads[4];
    thread serverThread;
    thread clientThread;

    while(true){
        cli(&pool, &clientThread, &serverThread, workerThreads);
    }
    return 0;
}
