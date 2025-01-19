#ifndef LB_H
#define LB_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>
#include <map>
#include <string>
#include <thread>
#include <sys/epoll.h>

#define MAX_SERVER_LIMIT    100
#define MAX_CLIENT_LIMIT    1000

struct ServerProp{
    std::string ip;
    int port;
    int avgLatency;
    int nreq;
    std::chrono::time_point<std::chrono::high_resolution_clock> curtime;

    ServerProp(std::string ip, int port);
};

typedef std::pair<std::string,int> ClientHash;
typedef std::pair<std::string,int> ServerHash;

class ServerPool{

    private:
    std::vector<ServerProp> mTable;
    std::map<int,int> mFdToServer;
    std::map<ClientHash, ServerHash> mClientToServer;
    int mCurIndex;

    public:

    int epollFd;
    epoll_event *eventArray;

    ServerPool();

    int nextServer(ClientHash *pCHash,char *serverIP, int &port);

    void addServer(const char *serverIP, int port);

    void listServer();
    
    bool checkHealth();

    void setTime(int index);

    void setLatency(int fd);

    void getServerData(std::vector<std::pair<std::string,int>> *pServerTable);

    void addIndex(int fd);
};

enum TaskType{
    LB_REQUEST,
    LB_RESPONSE,
    LB_DUMMY
};

struct Task{
    int fd;
    TaskType type;
};

extern std::atomic<bool> exitThread;
extern std::queue<Task> taskQueue;
extern std::mutex threadMutex;
extern std::condition_variable threadCondition;
extern std::map<int,ClientHash> fdToClient;
extern std::string _LB_IP;

void worker(ServerPool *pPool);

void setLOG(std::string s);

void printLOG(std::string s);

void lbExit(
        std::thread &serverThread,
        std::thread &clientThread,
        std::thread workerThreads[]);

#endif // !LB_H
