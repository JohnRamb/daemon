#include "network_daemon.h"
#include <iostream>
#include <system_error>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>

const char* SOCKET_PATH = "/tmp/network_daemon.sock";

NetworkDaemon::NetworkDaemon() 
    : netlink_mgr_(),
      unix_server_(loop_, SOCKET_PATH),
      network_mgr_(netlink_mgr_) {

    setupSignalHandlers();
    
    try {
        netlink_mgr_.init();
        
        // Устанавливаем callback для обработки netlink сообщений
        netlink_mgr_.setAddrCallback([this](struct nl_msg* msg) {
            // Здесь можно обрабатывать сообщения об адресах
            std::cout << "Получено сообщение об адресе" << std::endl;
        });
        
        netlink_mgr_.setLinkCallback([this](struct nl_msg* msg) {
            // Здесь можно обрабатывать сообщения о интерфейсах
            std::cout << "Получено сообщение об интерфейсе" << std::endl;
        });
        
        netlink_mgr_.setRouteCallback([this](struct nl_msg* msg) {
            // Здесь можно обрабатывать сообщения о маршрутах
            std::cout << "Получено сообщение о маршруте" << std::endl;
        });
        
        // Добавляем netlink сокет в цикл событий
        loop_.add(netlink_mgr_.getSocketFd(), EPOLLIN, 
            std::bind(&NetworkDaemon::handleNetlinkEvent, this, std::placeholders::_1, std::placeholders::_2));

        unix_server_.setClientHandler(
            std::bind(&NetworkDaemon::handleUnixCommand, this, std::placeholders::_1, std::placeholders::_2));
        unix_server_.start();
        
        std::cout << "NetlinkManager инициализирован успешно" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Ошибка инициализации NetlinkManager: " << e.what() << std::endl;
        throw;
    }
}

NetworkDaemon::~NetworkDaemon() {
    // Очистка ресурсов
}

void NetworkDaemon::setupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = [](int) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {}
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "Ошибка установки обработчика SIGCHLD");
    }
}

void NetworkDaemon::handleNetlinkEvent(int fd, uint32_t events) {
    try {
        netlink_mgr_.processEvents();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка обработки netlink событий: " << e.what() << std::endl;
    }
}

void NetworkDaemon::handleUnixCommand(int client_fd, const std::string& command) {
    processCommand(command, client_fd);
}

void NetworkDaemon::processCommand(const std::string& command, int client_fd) {
    // Обработка команд
    if (command.find("set_static ") == 0) {
        // Обработка set_static
    } else if (command.find("set_dynamic ") == 0) {
        // Обработка set_dynamic
    } else if (command == "get_status") {
        std::string status = network_mgr_.getStatus();
        unix_server_.sendResponse(client_fd, status);
    } else {
        unix_server_.sendResponse(client_fd, "Неизвестная команда");
    }
}

void NetworkDaemon::run() {
    std::cout << "Демон запущен, ожидание команд..." << std::endl;
    loop_.run();
}

void NetworkDaemon::stop() {
    loop_.stop();
}