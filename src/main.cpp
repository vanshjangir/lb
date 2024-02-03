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

using namespace std;

void parse(string rawInput, vector<string> &inputCommand){

    for(int i=0; i<(int)rawInput.size(); i++){
        int counter = 0;
        string temp;

        while(true){
            if(rawInput[counter] == ' ' || i == (int)rawInput.size()){
                i++;
                break;
            }

            temp += rawInput[i];
            counter++;
            i++;
        }
        inputCommand.push_back(temp);
    }
}

void threadExec(){
    
    while(!exitThread){
        task threadTask;
        {
            unique_lock<mutex> lock(threadMutex);
            threadCondition.wait(lock, [&]{ return !taskQueue.empty(); });
            if(exitThread){
                break;
            }

            threadTask = taskQueue.front();
            taskQueue.pop();
        }
        handleClient(threadTask);
    }
}

int main() {

    lbSocket lbClientSocket;
    epoll_event *epollClientFdArray;
    epoll_event *epollServerFdArray;
    map<int,int> *serverMap;

    serverMap = new map<int,int>;

    const int epollClientFd = epoll_create1(0);
    if(epollClientFd == -1){
        cerr << "error creating epoll";
        return -1;
    }

    const int epollServerFd = epoll_create1(0);
    if(epollServerFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    epollClientFdArray = new epoll_event[1000];
    epollServerFdArray = new epoll_event[1000];
    
    setupClientListener(lbClientSocket, LB_CLIENT_PORT, epollClientFd);

    thread clientThread(monitorClientFd, lbClientSocket, epollClientFd, epollClientFdArray);
    thread serverThread(monitorServerFd, serverMap, epollServerFd, epollServerFdArray);
    
    thread workerThreads[4] = {
        thread(threadExec),
        thread(threadExec),
        thread(threadExec),
        thread(threadExec)
    };

    while(true){
        
        string rawInput;
        vector<string> inputCommand;
        cout << "\nlb::";
        getline(cin,rawInput);
        
        parse(rawInput, inputCommand);

        if(inputCommand[0] == "exit"){
            exitThread = true;
            threadCondition.notify_all();
            serverThread.detach();
            clientThread.detach();
            for(int i=0; i<4; i++){
                workerThreads[i].join();
            }
            cout << "bye\n";
            exit(0);
        }
        else if(inputCommand[0] == "list"){
            // list the connected servers
        }
        else{
            cout << inputCommand[0] << endl;
        }
    }
}
