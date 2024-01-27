#ifndef HANDLE_H
#define HANDLE_H

#include <string>
#include <map>
#include "net.h"

void handleClient(task, std::map<int,int>*);
int sendToServer(char*, int, int, std::map<int,int>*);
int sendToClient(char*, int);
int addClientFdHeader(char*, int);

#endif // !HANDLE_H
