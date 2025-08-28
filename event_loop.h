#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <functional>
#include <map>
#include <sys/epoll.h>

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler);
    void remove(int fd);
    void run();
    void stop();

private:
    int epoll_fd_;
    std::map<int, std::function<void(int, uint32_t)>> handlers_;
    bool running_ = true;
};

#endif // EVENT_LOOP_H