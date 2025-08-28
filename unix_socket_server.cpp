#include "unix_socket_server.h"
#include "event_loop.h"
#include <iostream>
#include <system_error>
#include <cstring>
#include <unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>

UnixSocketServer::UnixSocketServer(EventLoop& loop, const std::string& socket_path)
    : loop_(loop), socket_path_(socket_path) {}

UnixSocketServer::~UnixSocketServer() {
    stop();
}

void UnixSocketServer::start() {
    createSocket();
    loop_.add(server_fd_, EPOLLIN, 
        std::bind(&UnixSocketServer::handleServerEvent, this, std::placeholders::_1, std::placeholders::_2));
}

void UnixSocketServer::stop() {
    if (server_fd_ != -1) {
        loop_.remove(server_fd_);
        close(server_fd_);
        unlink(socket_path_.c_str());
        server_fd_ = -1;
    }

    for (const auto& [fd, _] : client_handlers_) {
        loop_.remove(fd);
        close(fd);
    }
    client_handlers_.clear();
}

void UnixSocketServer::createSocket() {
    struct sockaddr_un addr;
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        throw std::system_error(errno, std::generic_category(), "Не удалось создать unix сокет");
    }
    
    unlink(socket_path_.c_str());
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(server_fd_);
        throw std::system_error(errno, std::generic_category(), "Не удалось привязать unix сокет");
    }
    
    if (listen(server_fd_, 5) == -1) {
        close(server_fd_);
        throw std::system_error(errno, std::generic_category(), "Не удалось прослушивать unix сокет");
    }
    
    chmod(socket_path_.c_str(), 0666);
    std::cout << "Unix сокет создан и прослушивается по пути " << socket_path_ << std::endl;
}

void UnixSocketServer::handleServerEvent(int fd, uint32_t events) {
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
    if (client_fd == -1) {
        std::cerr << "Ошибка принятия соединения: " << strerror(errno) << std::endl;
        return;
    }
    
    std::cout << "Новое клиентское соединение, fd=" << client_fd << std::endl;

    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Ошибка установки неблокирующего режима: " << strerror(errno) << std::endl;
        close(client_fd);
        return;
    }

    auto handler = std::bind(&UnixSocketServer::handleClientEvent, this, std::placeholders::_1, std::placeholders::_2);
    loop_.add(client_fd, EPOLLIN, handler);
    client_handlers_[client_fd] = handler;
}

void UnixSocketServer::handleClientEvent(int client_fd, uint32_t events) {
    char buffer[4096];
    ssize_t len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        if (len == 0) {
            std::cout << "Клиент (fd=" << client_fd << ") закрыл соединение" << std::endl;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "Ошибка чтения из клиентского сокета: " << strerror(errno) << std::endl;
        }
        cleanupClient(client_fd);
        return;
    }
    
    buffer[len] = '\0';
    std::string command(buffer);
    
    if (client_handler_) {
        client_handler_(client_fd, command);
    }
}

void UnixSocketServer::cleanupClient(int client_fd) {
    loop_.remove(client_fd);
    client_handlers_.erase(client_fd);
    close(client_fd);
}

void UnixSocketServer::setClientHandler(ClientHandler handler) {
    client_handler_ = handler;
}

void UnixSocketServer::sendResponse(int client_fd, const std::string& response) {
    if (send(client_fd, response.c_str(), response.length(), 0) < 0) {
        std::cerr << "Ошибка отправки ответа клиенту: " << strerror(errno) << std::endl;
        cleanupClient(client_fd);
    }
}