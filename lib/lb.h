#ifndef LB_H
#define LB_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>

enum taskType{
    LB_REQUEST,
    LB_RESPONSE,
};

struct task{
    int fd;
    taskType type;
    std::pair<int,std::map<int,int>*> serverInfo;
};

extern std::atomic<bool> exitThread;
extern std::queue<task> taskQueue;
extern std::mutex threadMutex;
extern std::condition_variable threadCondition;

void threadExec();

#endif // !LB_H
