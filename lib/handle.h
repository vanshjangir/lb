#ifndef HANDLE_H
#define HANDLE_H

#include <string>
#include <map>
#include "lb.h"

/* Handles a task from the task queue */
void handleTask(task qtask, ServerPool* pPool);

/* send the recieved client request to the server */
int sendToServer(char *buffer, int buflen, task& clientTask, ServerPool* pPool);

/* send the received server response to client */
int sendToClient(char *buffer, int buflen);

/* Add the client socket file descriptor to the response */
int addClientFdHeader(char *header, int fd);

/* Parse the clients socket file descriptor to recognize the client */
int getClientResponseFd(char* buffer, int buflen);

#endif // !HANDLE_H
