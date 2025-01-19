#ifndef HANDLE_H
#define HANDLE_H

#include <string>
#include "lb.h"

void handleTask(Task qTask, ServerPool* pPool);

int sendToServer(char *buffer, int buflen, Task& clientTask, ServerPool* pPool);

int sendToClient(char *buffer, int buflen);

int addClientFdHeader(char *header, int fd);

int getClientResponseFd(char* buffer, int buflen);

ClientHash getClientHash(int fd);

#endif // !HANDLE_H
