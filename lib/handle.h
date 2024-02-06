#ifndef HANDLE_H
#define HANDLE_H

#include <string>
#include <map>
#include "lb.h"

void handleTask(task);
int sendToServer(char*, int, task&); 
int sendToClient(char*, int);
int addClientFdHeader(char*, int);
int getClientResponseFd(char*, int);

#endif // !HANDLE_H
