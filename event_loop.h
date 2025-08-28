#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <event2/event.h>
#include <functional>
#include <map>
#include <stdexcept>

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // Добавляет дескриптор в цикл событий
    void add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler);

    // Удаляет дескриптор из цикла событий
    void remove(int fd);

    // Запускает цикл событий
    void run();

    // Останавливает цикл событий
    void stop();

    // Возвращает event_base для использования в других компонентах
    struct event_base* get_event_base() const { return base_; }

private:
    struct event_base* base_; // Основной объект libevent
    std::map<int, struct event*> events_; // Хранилище событий
    std::map<int, std::function<void(int, uint32_t)>> handlers_; // Хранилище обработчиков
    bool running_ = true;

    // Колбэк для обработки событий
    static void event_callback(evutil_socket_t fd, short events, void* arg);
};

#endif // EVENT_LOOP_H