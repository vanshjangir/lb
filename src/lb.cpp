#include <iostream>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>

#include "../lib/net.h"
#include "../lib/lb.h"
#include "../lib/handle.h"

std::atomic<bool> exitThread(false);
std::queue<task> taskQueue;
std::mutex threadMutex;
std::condition_variable threadCondition;

bool _IS_RUNNING = false;

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
void parse_input(string rawInput, vector<string> &inputArgs){

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

void threadExec(ServerPool *pPool){
    
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
    
    workerThreads[0] = thread(threadExec, &pool);
    workerThreads[1] = thread(threadExec, &pool);
    workerThreads[2] = thread(threadExec, &pool);
    workerThreads[3] = thread(threadExec, &pool);

    _IS_RUNNING = true;
    return 0;
}

void lbExit(
        thread &serverThread,
        thread &clientThread,
        thread workerThreads[])
{
    
    if(!_IS_RUNNING)
        goto out;
    
    exitThread = true;

    {
        task dummy;
        dummy.type = LB_DUMMY;
        unique_lock<mutex> lock(threadMutex);
        taskQueue.push(dummy);
    }

    threadCondition.notify_all();
    serverThread.detach();
    clientThread.detach();

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
        
        parse_input(rawInput, inputArgs);

        if(inputArgs[0] == "exit"){
            lbExit(serverThread, clientThread, workerThreads);
        }
        else if(inputArgs[0] == "run"){
            int rc;

            if(!pool.checkHealth()){
                cout << "zero servers found\n";
                lbExit(serverThread, clientThread, workerThreads);
            }

            rc = runLB(pool, serverThread, clientThread, workerThreads);
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
