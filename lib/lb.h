#ifndef LB_H
#define LB_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <sys/epoll.h>

#define MAX_SERVER_LIMIT 100
#define MAX_CLIENT_LIMIT 1000


class ServerPool{

    private:
    std::vector<std::pair<std::string,int>> mTable;
    int mCurIndex;
    int mCurWeight;

    public:

    int epollFd;
    epoll_event *eventArray;

    ServerPool();

    void nextIP(char *serverIP);

    void addServer(const char *serverIP, int weight);

    void listServer();
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

#endif // !LB_H
