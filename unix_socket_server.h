#ifndef UNIX_SOCKET_SERVER_H
#define UNIX_SOCKET_SERVER_H

#include "event_loop.h"
#include <functional>
#include <map>
#include <string>
#include <chrono>

class UnixSocketServer {
public:
    using ClientHandler = std::function<void(int, const std::string&)>;

    UnixSocketServer(EventLoop& loop, const std::string& socket_path);
    ~UnixSocketServer();

    void start();
    void stop();
    void setClientHandler(ClientHandler handler);
    void sendResponse(int client_fd, const std::string& response);
    void broadcastToAllClients(const std::string& message);

private:
    EventLoop& loop_;
    std::string socket_path_;
    int server_fd_ = -1;
    std::map<int, std::function<void(int, uint32_t)>> client_handlers_; // Изменён тип
    ClientHandler client_handler_;
    std::map<int, std::chrono::steady_clock::time_point> client_last_activity_;
    struct event* timer_event_ = nullptr;

    void createSocket();
    void handleServerEvent(int fd, uint32_t events);
    void handleClientEvent(int client_fd, uint32_t events);
    void cleanupClient(int client_fd);
    void checkTimeouts();
    static void timer_callback(evutil_socket_t fd, short events, void* arg);
};

#endif // UNIX_SOCKET_SERVER_H