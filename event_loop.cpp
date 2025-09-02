#include "event_loop.h"
#include <iostream>
#include <system_error>
#include <cstring>
#include <memory>
#include <event2/event.h>
#include <event2/thread.h>
#include <sys/epoll.h>

EventLoop::EventLoop() {
#if LIBEVENT_VERSION_NUMBER >= 0x02001500
    evthread_use_pthreads();
#endif

    base_ = event_base_new();
    if (!base_) {
        throw std::runtime_error("Не удалось создать event_base");
    }
}

EventLoop::~EventLoop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    
    // Очищаем события
    events_.clear();
    event_data_.clear();
    
    if (base_) {
        event_base_free(base_);
        base_ = nullptr;
    }
}

void EventLoop::eventCallback(evutil_socket_t fd, short events, void* arg) {
    auto* eventData = static_cast<EventData*>(arg);
    if (eventData && eventData->handler) {
        eventData->handler(fd, events);
    }
}

void EventLoop::add(int fd, short events, std::function<void(int, short)> handler) {
    if (fd < 0) {
        throw std::invalid_argument("Недопустимый файловый дескриптор");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    if (events_.find(fd) != events_.end()) {
        throw std::invalid_argument("Дескриптор уже зарегистрирован");
    }

    short libevent_events = events | EV_PERSIST;

    // Создаем данные события
    auto eventData = std::make_unique<EventData>(std::move(handler), fd);
    
    // Создаем событие
    event* ev = event_new(base_, fd, libevent_events, EventLoop::eventCallback, eventData.get());
    if (!ev) {
        throw std::runtime_error("Не удалось создать событие для fd");
    }

    if (event_add(ev, nullptr) == -1) {
        event_free(ev);
        throw std::runtime_error("Не удалось добавить событие в event_base");
    }

    // Сохраняем событие и данные
    events_[fd] = std::make_unique<event>(ev);
    event_data_[fd] = std::move(eventData);
}

void EventLoop::modify(int fd, short events, std::function<void(int, short)> handler) {
    if (fd < 0) {
        throw std::invalid_argument("Недопустимый файловый дескриптор");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    auto eventIt = events_.find(fd);
    auto dataIt = event_data_.find(fd);
    
    if (eventIt == events_.end() || dataIt == event_data_.end()) {
        throw std::invalid_argument("Дескриптор не зарегистрирован");
    }

    // Обновляем обработчик
    if (handler) {
        dataIt->second->handler = std::move(handler);
    }

    // Удаляем старое событие
    if (event_del(eventIt->second.get()) == -1) {
        throw std::runtime_error("Не удалось удалить событие для модификации");
    }

    // Создаем новое событие с обновленными флагами
    short libevent_events = events | EV_PERSIST;
    event* new_ev = event_new(base_, fd, libevent_events, EventLoop::eventCallback, dataIt->second.get());
    if (!new_ev) {
        // Пытаемся восстановить старое событие
        event_add(eventIt->second.get(), nullptr);
        throw std::runtime_error("Не удалось создать новое событие");
    }

    // Заменяем событие
    eventIt->second.reset(new_ev);

    // Добавляем новое событие
    if (event_add(new_ev, nullptr) == -1) {
        throw std::runtime_error("Не удалось добавить модифицированное событие");
    }
}

bool EventLoop::remove(int fd) {
    if (fd < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    auto eventIt = events_.find(fd);
    auto dataIt = event_data_.find(fd);
    
    if (eventIt == events_.end() || dataIt == event_data_.end()) {
        return false;
    }

    // Удаляем событие из event_base
    if (event_del(eventIt->second.get()) == -1) {
        std::cerr << "Не удалось удалить событие для fd " << fd << std::endl;
        return false;
    }

    // Удаляем из хранилищ
    events_.erase(eventIt);
    event_data_.erase(dataIt);
    return true;
}

void EventLoop::run() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            throw std::runtime_error("Цикл событий уже запущен");
        }
        running_ = true;
    }

    if (event_base_dispatch(base_) == -1) {
        throw std::runtime_error("Ошибка в event_base_dispatch");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
}

void EventLoop::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (base_ && running_) {
        if (event_base_loopbreak(base_) == -1) {
            std::cerr << "Не удалось остановить цикл событий" << std::endl;
        }
    }
}