#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <event2/event.h>

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void add(int fd, short events, std::function<void(int, short)> handler);
    void modify(int fd, short events, std::function<void(int, short)> handler);
    bool remove(int fd);
    void run();
    void stop();

private:
    // Вспомогательная структура для хранения информации о событии
    struct EventData {
        std::function<void(int, short)> handler;
        int fd;
        
        EventData(std::function<void(int, short)> h, int f) 
            : handler(std::move(h)), fd(f) {}
    };

    static void eventCallback(evutil_socket_t fd, short events, void* arg);

    event_base* base_;
    std::map<int, std::unique_ptr<event>> events_; // Храним события как unique_ptr
    std::map<int, std::unique_ptr<EventData>> event_data_; // Храним данные отдельно
    std::mutex mutex_;
    bool running_ = false;
};

#endif // EVENT_LOOP_H