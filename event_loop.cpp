#include "event_loop.h"
#include <iostream>
#include <sys/epoll.h>

EventLoop::EventLoop() {
    base_ = event_base_new();
    if (!base_) {
        throw std::runtime_error("Не удалось создать event_base");
    }
}

EventLoop::~EventLoop() {
    for (auto& [fd, ev] : events_) {
        event_free(ev);
    }
    event_base_free(base_);
}

void EventLoop::add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler) {
    // Переводим epoll-события в libevent
    short libevent_flags = 0;
    if (events & EPOLLIN) libevent_flags |= EV_READ;
    if (events & EPOLLOUT) libevent_flags |= EV_WRITE;
    libevent_flags |= EV_PERSIST; // Событие не одноразовое

    struct event* ev = event_new(base_, fd, libevent_flags, event_callback, this);
    if (!ev) {
        throw std::runtime_error("Не удалось создать событие");
    }
    if (event_add(ev, nullptr) == -1) {
        event_free(ev);
        throw std::runtime_error("Не удалось добавить событие");
    }

    events_[fd] = ev;
    handlers_[fd] = handler;
}

void EventLoop::remove(int fd) {
    auto it = events_.find(fd);
    if (it != events_.end()) {
        event_free(it->second);
        events_.erase(it);
        handlers_.erase(fd);
    }
}

void EventLoop::run() {
    running_ = true;
    if (event_base_dispatch(base_) == -1) {
        throw std::runtime_error("Ошибка в event_base_dispatch");
    }
}

void EventLoop::stop() {
    running_ = false;
    event_base_loopexit(base_, nullptr);
}

void EventLoop::event_callback(evutil_socket_t fd, short events, void* arg) {
    EventLoop* loop = static_cast<EventLoop*>(arg);
    uint32_t epoll_events = 0;
    if (events & EV_READ) epoll_events |= EPOLLIN;
    if (events & EV_WRITE) epoll_events |= EPOLLOUT;

    auto it = loop->handlers_.find(fd);
    if (it != loop->handlers_.end()) {
        it->second(fd, epoll_events);
    }
}