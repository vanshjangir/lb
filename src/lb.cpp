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

/* Parse the input char array into a vector of strings*/
void parse_input(string rawInput, vector<string> &inputArgs){

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
        inputArgs.push_back(temp);
    }
}

void threadExec(){
    
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
        handleTask(qtask);
    }
}

int main(int argc, char **argv){

    if(argc>1){

    }

    lbSocket lbClientSocket;
    epoll_event *epollClientFdArray;
    epoll_event *epollServerFdArray;
    map<int,int> *serverMap;
    const int epollClientFd = epoll_create1(0);
    const int epollServerFd = epoll_create1(0);

    if(epollClientFd == -1){
        cerr << "error creating epoll";
        return -1;
    }

    if(epollServerFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    serverMap = new map<int,int>;
    epollClientFdArray = new epoll_event[1000];
    epollServerFdArray = new epoll_event[1000];

    setupClientListener(lbClientSocket, LB_CLIENT_PORT, epollClientFd);

    thread clientThread(
            monitorClientFd,
            lbClientSocket,
            epollClientFd,
            epollServerFd,
            epollClientFdArray,
            serverMap);

    thread serverThread(
            monitorServerFd,
            epollServerFd,
            epollServerFdArray,
            serverMap);
    
    thread workerThreads[4] = {
        thread(threadExec),
        thread(threadExec),
        thread(threadExec),
        thread(threadExec)
    };

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
        else if(inputArgs[0] == "list"){
            // list the connected servers
        }
        else{
            cout << "invalid command" << endl;
        }
    }
}
