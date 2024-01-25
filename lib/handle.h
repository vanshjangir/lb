#ifndef HANDLE_H
#define HANDLE_H

#include <string>
#include <map>
#include "net.h"

void handleClient(task, std::map<int,int>*);
int sendToServer(char*, int, std::map<int,int>*);
void getNextServerIP(char*);
int addClientFdHeader(char*, int);

#endif // !HANDLE_H
