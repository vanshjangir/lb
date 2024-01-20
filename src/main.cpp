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
            if(rawInput[counter] == ' ' ||
                    i == (int)rawInput.size()){
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

    int epollFd;
    epoll_event *epollFdArray;
    lbSocket lbClientSocket;

    epollFd = epoll_create1(0);
    if(epollFd == -1){
        cerr << "error creating epoll";
        return -1;
    }
    
    epollFdArray = new epoll_event[1000];
    
    setupSocket(lbClientSocket, LB_CLIENT_PORT, epollFd);

    thread clientThread(monitorFd,
            lbClientSocket,
            epollFd,
            epollFdArray);
    
    thread workerThreads[4] = {
        thread(threadExec),
        thread(threadExec),
        thread(threadExec),
        thread(threadExec)
    };

    while(true){
        
        string rawInput;
        vector<string> inputCommand;
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
