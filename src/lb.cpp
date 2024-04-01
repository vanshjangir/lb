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
int LOG_VERBOSITY = 0;

using namespace std;

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

ServerPool::ServerPool(){
    mTable.push_back({"127.0.0.1", 3});
    mTable.push_back({"127.0.0.1", 1});
    mCurIndex = 0;
    mCurWeight = 3;
}

void ServerPool::nextIP(char *serverIP){
    if(mCurWeight <= 0){
        mCurIndex = (mCurIndex+1)%(mTable.size());
        mCurWeight = mTable[mCurIndex].second;
    }

    int i;
    for(i=0; i<mTable[mCurIndex].first.size(); i++){
        serverIP[i] = mTable[mCurIndex].first[i];
    }
    serverIP[i] = '\0';
    mCurWeight--;
}

void ServerPool::listServer(){
    cout << "\nIP Address" << "\t" << "Weight" << endl;
    for(int i=0; i<mTable.size(); i++){
        cout << mTable[i].first << "\t" << mTable[i].second << endl;
    }
}

void ServerPool::addServer(const char *serverIP, int weight){

    /* testing the server before pushing,
     * not implemented yet
     */

    string s(serverIP);
    mTable.push_back({s,weight});
}

int runLB(ServerPool &pool ,thread &serverThread, thread &clientThread, thread workerThreads[]){

    lbSocket lbClientSocket;
    epoll_event *epollClientFdArray;

    const int epollClientFd = epoll_create1(0);
    if(epollClientFd == -1){
        cerr << "error creating epoll";
        return -1;
    }

    pool.epollFd = epoll_create1(0);
    if(pool.epollFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    pool.eventArray = new epoll_event[MAX_SERVER_LIMIT];
    epollClientFdArray = new epoll_event[MAX_CLIENT_LIMIT];

    setupClientListener(lbClientSocket, LB_CLIENT_PORT, epollClientFd);

    clientThread = thread(
            monitorClientFd,
            lbClientSocket,
            epollClientFd,
            pool.epollFd,
            epollClientFdArray);

    serverThread = thread(monitorServerFd, &pool);
    
    workerThreads[0] = thread(threadExec, &pool);
    workerThreads[1] = thread(threadExec, &pool);
    workerThreads[2] = thread(threadExec, &pool);
    workerThreads[3] = thread(threadExec, &pool);

    return 0;
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

            cout << "Exiting...";
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

            cout << "bye\n";
            exit(0);
        }
        else if(inputArgs[0] == "run"){
            int rc;
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
            if(inputArgs.size() == 3){
                pool.addServer(inputArgs[1].c_str(), stoi(inputArgs[2]));
            }else{
                cout << "not enough arguments\n";
            }
        }
        else{
            cout << "invalid command" << endl;
        }
    }
}
