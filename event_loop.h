#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <functional>
#include <map>
#include <mutex>
#include <sys/epoll.h>
#include <vector>

class EventLoop {
public:
    // Конструктор. Создает epoll-инстанс.
    // @throws std::system_error при ошибке создания epoll.
    EventLoop();

    // Деструктор. Закрывает epoll и очищает ресурсы.
    ~EventLoop();

    // Добавляет файловый дескриптор в цикл событий.
    // @param fd Файловый дескриптор.
    // @param events Битовая маска событий (например, EPOLLIN | EPOLLOUT).
    // @param handler Функция-обработчик, вызываемая при срабатывании события.
    // @throws std::system_error при ошибке epoll_ctl.
    // @throws std::invalid_argument если fd недействителен.
    void add(int fd, uint32_t events, std::function<void(int, uint32_t)> handler);

    // Модифицирует события для зарегистрированного дескриптора.
    // @param fd Файловый дескриптор.
    // @param events Новая битовая маска событий.
    // @param handler Новый обработчик (опционально).
    // @throws std::system_error при ошибке epoll_ctl.
    // @throws std::invalid_argument если fd недействителен или не зарегистрирован.
    void modify(int fd, uint32_t events, std::function<void(int, uint32_t)> handler);

    // Удаляет файловый дескриптор из цикла событий.
    // @param fd Файловый дескриптор.
    // @returns true при успешном удалении, false если дескриптор не был зарегистрирован.
    bool remove(int fd);

    // Запускает цикл событий. Блокирует до вызова stop().
    // @throws std::system_error при ошибке epoll_wait.
    void run();

    // Останавливает цикл событий.
    void stop() const;

private:
    static constexpr size_t kMaxEvents = 128; // Максимальное количество событий за один epoll_wait.
    int epoll_fd_; // Дескриптор epoll.
    std::map<int, std::function<void(int, uint32_t)>> handlers_; // Хранилище обработчиков.
    mutable std::mutex mutex_; // Мьютекс для потокобезопасности.
    mutable bool running_ = true; // Флаг выполнения цикла.
};

#endif // EVENT_LOOP_H