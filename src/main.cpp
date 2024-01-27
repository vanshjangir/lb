#include <iostream>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

#include "../lib/net.h"

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

int main() {

    int epollClientFd;
    int epollServerFd;
    epoll_event *epollClientFdArray;
    epoll_event *epollServerFdArray;
    lbSocket lbClientSocket;

    epollClientFd = epoll_create1(0);
    if(epollClientFd == -1){
        cerr << "error creating epoll";
        return -1;
    }

    epollServerFd = epoll_create1(0);
    if(epollServerFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    epollClientFdArray = new epoll_event[1000];
    epollServerFdArray = new epoll_event[1000];
    
    setupClientListener(lbClientSocket, LB_CLIENT_PORT, epollClientFd);

    thread clientThread(monitorClientFd,
            lbClientSocket,
            epollClientFd,
            epollClientFdArray);

    thread serverThread(monitorServerFd,
            epollServerFd,
            epollServerFdArray);
    
    thread workerThreads[4] = {
        thread(threadExec, epollServerFd),
        thread(threadExec, epollServerFd),
        thread(threadExec, epollServerFd),
        thread(threadExec, epollServerFd)
    };

    while(true){
        
        string rawInput;
        vector<string> inputCommand;
        cout << "\nlb::";
        getline(cin,rawInput);
        
        parse(rawInput, inputCommand);

        if(inputCommand[0] == "exit"){
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
