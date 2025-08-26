#ifndef UNIX_SOCKET_SERVER_H
#define UNIX_SOCKET_SERVER_H

#include <string>
#include <functional>
#include <map>
#include <cstdint>

class EventLoop;

class UnixSocketServer {
public:
    using ClientHandler = std::function<void(int, const std::string&)>;

    UnixSocketServer(EventLoop& loop, const std::string& socket_path);
    ~UnixSocketServer();

    void start();
    void stop();
    void setClientHandler(ClientHandler handler);
    void sendResponse(int client_fd, const std::string& response);

private:
    EventLoop& loop_;
    std::string socket_path_;
    int server_fd_ = -1;
    ClientHandler client_handler_;
    std::map<int, std::function<void(int, uint32_t)>> client_handlers_;

    void createSocket();
    void handleServerEvent(int fd, uint32_t events);
    void handleClientEvent(int client_fd, uint32_t events);
    void cleanupClient(int client_fd);
};

#endif // UNIX_SOCKET_SERVER_H