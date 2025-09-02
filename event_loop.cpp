#include "event_loop.h"
#include <iostream>
#include <system_error>
#include <cstring>
#include <unistd.h>
#include <vector>

EventLoop::EventLoop() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::system_error(errno, std::generic_category(), "Не удалось создать epoll");
    }
}

EventLoop::~EventLoop() {
    // Удаляем все зарегистрированные дескрипторы
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [fd, _] : handlers_) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            std::cerr << "Не удалось удалить fd " << fd << " из epoll: " << strerror(errno) << std::endl;
        }
    }
    handlers_.clear();
    if (close(epoll_fd_) == -1) {
        std::cerr << "Ошибка при закрытии epoll_fd: " << strerror(errno) << std::endl;
    }
}

void EventLoop::add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler) {
    if (fd < 0) {
        throw std::invalid_argument("Недопустимый файловый дескриптор");
    }
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[fd] = handler;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        handlers_.erase(fd); // Откат при ошибке
        throw std::system_error(errno, std::generic_category(), "Не удалось добавить fd в epoll");
    }
}

void EventLoop::modify(int fd, uint32_t events, std::function<void(int, uint32_t)> handler) {
    if (fd < 0) {
        throw std::invalid_argument("Недопустимый файловый дескриптор");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (handlers_.find(fd) == handlers_.end()) {
        throw std::invalid_argument("Дескриптор не зарегистрирован");
    }
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    handlers_[fd] = handler;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        throw std::system_error(errno, std::generic_category(), "Не удалось модифицировать fd в epoll");
    }
}

bool EventLoop::remove(int fd) {
    if (fd < 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (handlers_.find(fd) == handlers_.end()) {
        std::cerr << "Попытка удалить незарегистрированный fd: " << fd << std::endl;
        return false;
    }
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        std::cerr << "Не удалось удалить fd из epoll: " << strerror(errno) << std::endl;
        return false;
    }
    handlers_.erase(fd);
    return true;
}

void EventLoop::run() {
    std::vector<epoll_event> events(kMaxEvents);
    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) break;
        }
        int nfds = epoll_wait(epoll_fd_, events.data(), kMaxEvents, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "Ошибка epoll_wait");
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            std::function<void(int, uint32_t)> handler;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = handlers_.find(fd);
                if (it == handlers_.end()) continue;
                handler = it->second;
            }
            handler(fd, ev);
        }
    }
}

void EventLoop::stop() const {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}