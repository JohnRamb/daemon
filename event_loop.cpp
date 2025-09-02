#include "event_loop.h"
#include <iostream>
#include <system_error>
#include <cstring>
#include <unistd.h>

EventLoop::EventLoop() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::system_error(errno, std::generic_category(), "Не удалось создать epoll");
    }
}

EventLoop::~EventLoop() {
    close(epoll_fd_);
}

void EventLoop::add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    handlers_[fd] = handler;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw std::system_error(errno, std::generic_category(), "Не удалось добавить fd в epoll");
    }
}

void EventLoop::remove(int fd) {
    if (handlers_.find(fd) == handlers_.end()) {
        std::cerr << "Attempted to remove unregistered fd: " << fd << std::endl;
        return;
    }
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        std::cerr << "Не удалось удалить fd из epoll: " << strerror(errno) << std::endl;
    }
    handlers_.erase(fd);
}

void EventLoop::run() {
    struct epoll_event events[10];
    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events, 10, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "Ошибка epoll_wait");
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            if (handlers_.count(fd)) {
                handlers_[fd](fd, ev);
            }
        }
    }
}

void EventLoop::stop() {
    running_ = false;
}