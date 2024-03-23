#ifndef HANDLE_H
#define HANDLE_H

#include <string>
#include <map>
#include "lb.h"

/* Handles a task from the task queue */
void handleTask(task);

/* send the recieved client request to the server */
int sendToServer(char*, int, task&); 

/* send the received server response to client */
int sendToClient(char*, int);

/* Add the client socket file descriptor to the response */
int addClientFdHeader(char*, int);

/* Parse the clients socket file descriptor to recognize the client */
int getClientResponseFd(char*, int);

#endif // !HANDLE_H
