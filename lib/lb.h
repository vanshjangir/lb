#ifndef LB_H
#define LB_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <thread>
#include <sys/epoll.h>

#define MAX_SERVER_LIMIT 100
#define MAX_CLIENT_LIMIT 1000

struct ServerProp{
    std::string ip;
    int port;
    int weight;
    int avgLatency;

    ServerProp(std::string ip, int port, int weight, int avgLatency);
};

class ServerPool{

    private:
    std::vector<ServerProp> mTable;
    int mCurIndex;
    int mCurWeight;

    public:

    int epollFd;
    epoll_event *eventArray;

    ServerPool();

    void nextServer(char *serverIP, int &port);

    void addServer(const char *serverIP, int port, int weight);

    void listServer();
    
    bool checkHealth();
};

enum taskType{
    LB_REQUEST,
    LB_RESPONSE,
    LB_DUMMY
};

struct task{
    int fd;
    taskType type;
};

extern std::atomic<bool> exitThread;
extern std::queue<task> taskQueue;
extern std::mutex threadMutex;
extern std::condition_variable threadCondition;

void threadExec(ServerPool *pPool);

void lbExit(
        std::thread &serverThread,
        std::thread &clientThread,
        std::thread workerThreads[]);

#endif // !LB_H
